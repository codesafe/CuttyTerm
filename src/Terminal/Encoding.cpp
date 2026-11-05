#include "Encoding.h"
#include <Windows.h>

namespace Cutty
{
    // bytes 전체가 (마지막에 잘린 시퀀스를 제외하고) 유효한 UTF-8 인지 검사한다.
    // 순수 ASCII 만 있는 경우도 "유효함"으로 본다 (ASCII 는 UTF-8 의 부분집합이므로,
    // 아직 멀티바이트 문자가 나오지 않았다는 것만으로 ANSI 라고 단정할 수 없다).
    bool StreamDecoder::LooksLikeValidUtf8(const std::vector<uint8_t>& bytes)
    {
        size_t i = 0;
        const size_t n = bytes.size();

        while (i < n)
        {
            uint8_t b0 = bytes[i];

            if (b0 < 0x80)
            {
                ++i;
                continue;
            }

            int extra;
            char32_t minValue;

            if ((b0 & 0xE0) == 0xC0) { extra = 1; minValue = 0x80; }
            else if ((b0 & 0xF0) == 0xE0) { extra = 2; minValue = 0x800; }
            else if ((b0 & 0xF8) == 0xF0) { extra = 3; minValue = 0x10000; }
            else { return false; } // 잘못된 선행 바이트

            if (i + extra >= n)
            {
                // 스트림 끝에서 잘린 시퀀스 - 다음 청크를 기다려야 하므로 실패로 보지 않음
                break;
            }

            char32_t value = b0 & (0x3F >> extra);
            for (int k = 1; k <= extra; ++k)
            {
                uint8_t bk = bytes[i + k];
                if ((bk & 0xC0) != 0x80) return false;
                value = (value << 6) | (bk & 0x3F);
            }

            if (value < minValue) return false; // 과다 인코딩(overlong) 거부
            i += extra + 1;
        }

        return true;
    }

    std::vector<char32_t> StreamDecoder::DecodeUtf8(const std::vector<uint8_t>& bytes, std::vector<uint8_t>& leftover)
    {
        std::vector<char32_t> out;
        out.reserve(bytes.size());
        size_t i = 0;
        const size_t n = bytes.size();

        while (i < n)
        {
            uint8_t b0 = bytes[i];

            if (b0 < 0x80)
            {
                out.push_back(b0);
                ++i;
                continue;
            }

            int extra;
            if ((b0 & 0xE0) == 0xC0) extra = 1;
            else if ((b0 & 0xF0) == 0xE0) extra = 2;
            else if ((b0 & 0xF8) == 0xF0) extra = 3;
            else { out.push_back(0xFFFD); ++i; continue; } // 알 수 없는 바이트는 대체문자

            if (i + extra >= n)
            {
                // 잘린 시퀀스: 다음 Decode() 호출로 이월
                leftover.assign(bytes.begin() + i, bytes.end());
                break;
            }

            char32_t value = b0 & (0x3F >> extra);
            bool ok = true;
            for (int k = 1; k <= extra; ++k)
            {
                uint8_t bk = bytes[i + k];
                if ((bk & 0xC0) != 0x80) { ok = false; break; }
                value = (value << 6) | (bk & 0x3F);
            }

            out.push_back(ok ? value : 0xFFFD);
            i += extra + 1;
        }

        return out;
    }

    std::vector<char32_t> StreamDecoder::DecodeAnsi(const std::vector<uint8_t>& bytesIn, unsigned codePage,
                                                    std::vector<uint8_t>& leftover)
    {
        std::vector<char32_t> out;
        if (bytesIn.empty()) return out;

        // CP949/Shift-JIS 같은 2바이트(DBCS) 인코딩은 TCP 청크 끝에서 선행 바이트만 오고
        // 후행 바이트가 다음 청크로 넘어갈 수 있다. 끝에 잘린 선행 바이트는 다음 호출로 넘긴다.
        size_t usable = bytesIn.size();
        for (size_t i = 0; i < bytesIn.size();)
        {
            if (IsDBCSLeadByteEx(codePage, bytesIn[i]))
            {
                if (i + 1 >= bytesIn.size()) { usable = i; break; }
                i += 2;
            }
            else
            {
                ++i;
            }
        }
        leftover.assign(bytesIn.begin() + usable, bytesIn.end());
        if (usable == 0) return out;
        const char* bytes = reinterpret_cast<const char*>(bytesIn.data());

        int wideLen = MultiByteToWideChar(codePage, 0, bytes, static_cast<int>(usable), nullptr, 0);
        if (wideLen <= 0) return out;

        std::wstring wide(wideLen, L'\0');
        MultiByteToWideChar(codePage, 0, bytes, static_cast<int>(usable), wide.data(), wideLen);

        // UTF-16 -> UTF-32 (서로게이트 쌍 처리)
        out.reserve(wide.size());
        for (size_t i = 0; i < wide.size(); ++i)
        {
            wchar_t c = wide[i];
            if (c >= 0xD800 && c <= 0xDBFF && i + 1 < wide.size())
            {
                wchar_t low = wide[i + 1];
                if (low >= 0xDC00 && low <= 0xDFFF)
                {
                    char32_t cp = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
                    out.push_back(cp);
                    ++i;
                    continue;
                }
            }
            out.push_back(static_cast<char32_t>(c));
        }

        return out;
    }

    std::vector<char32_t> StreamDecoder::Decode(const uint8_t* data, size_t length)
    {
        std::vector<uint8_t> bytes = mPending;
        bytes.insert(bytes.end(), data, data + length);
        mPending.clear();

        if (mCodePage == kCodePageUtf8) return DecodeUtf8(bytes, mPending);

        if (mCodePage == kCodePageAuto && !mConfirmedAnsi)
        {
            if (LooksLikeValidUtf8(bytes))
            {
                return DecodeUtf8(bytes, mPending);
            }
            // 유효하지 않은 UTF-8 시퀀스를 발견 - 이 연결은 UTF-8 이 아니라고 확정하고
            // 이후로는 계속 ANSI 로 디코딩한다.
            mConfirmedAnsi = true;
        }

        return DecodeAnsi(bytes, EffectiveCodePage(), mPending);
    }

    unsigned StreamDecoder::EffectiveCodePage() const
    {
        if (mCodePage != kCodePageAuto) return mCodePage;
        return mConfirmedAnsi ? GetACP() : kCodePageUtf8;
    }

    std::string StreamDecoder::Encode(const std::wstring& text) const
    {
        if (text.empty()) return {};
        unsigned codePage = EffectiveCodePage();
        int len = WideCharToMultiByte(codePage, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        if (len <= 0) return {};
        std::string result(len, '\0');
        WideCharToMultiByte(codePage, 0, text.c_str(), static_cast<int>(text.size()), result.data(), len, nullptr, nullptr);
        return result;
    }
}
