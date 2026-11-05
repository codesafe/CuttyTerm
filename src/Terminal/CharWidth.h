#pragma once
#include <cstdint>
#include <string>

namespace Cutty
{
    // 유니코드 코드포인트가 터미널 셀 2칸을 차지하는 "넓은 문자"(한중일 등)인지 판별.
    // 완벽한 East-Asian-Width 테이블은 아니지만 실사용에 필요한 주요 구간을 포함한다.
    inline bool IsWideChar(char32_t codePoint)
    {
        static const struct Range { char32_t first, last; } wideRanges[] =
        {
            { 0x1100, 0x115F },  // 한글 자모
            { 0x2E80, 0x303E },  // CJK 부수/구두점
            { 0x3041, 0x33FF },  // 히라가나/가타카나/CJK 기호
            { 0x3400, 0x4DBF },  // CJK 확장 A
            { 0x4E00, 0x9FFF },  // CJK 통합 한자
            { 0xA000, 0xA4CF },  // 이 자모(이족 문자)
            { 0xAC00, 0xD7A3 },  // 한글 음절
            { 0xF900, 0xFAFF },  // CJK 호환 한자
            { 0xFF00, 0xFF60 },  // 전각 폼
            { 0xFFE0, 0xFFE6 },  // 전각 기호
            { 0x20000, 0x2FFFD }, // CJK 확장 B 이상
            { 0x30000, 0x3FFFD },
        };

        for (const auto& range : wideRanges)
        {
            if (codePoint >= range.first && codePoint <= range.last)
            {
                return true;
            }
        }

        return false;
    }

    // 기본 폰트(예: Consolas 등 고정폭 라틴 폰트)에 글리프가 없을 가능성이 높은
    // 문자에 대해 대체 폰트 패밀리 이름을 돌려준다. 없으면 빈 문자열(기본 폰트 사용).
    // DirectWrite 의 전체 폰트 폴백(IDWriteFontFallback) 대신, 한/영/일 표시라는
    // 요구사항에 맞춘 간단한 유니코드 구간 기반 휴리스틱이다.
    inline const wchar_t* GetFallbackFontFamily(char32_t codePoint)
    {
        if (codePoint < 0x80) return nullptr; // ASCII 는 기본 폰트로 충분

        // 한글 자모/음절
        if ((codePoint >= 0x1100 && codePoint <= 0x11FF) ||
            (codePoint >= 0x3130 && codePoint <= 0x318F) ||
            (codePoint >= 0xAC00 && codePoint <= 0xD7A3))
        {
            return L"Malgun Gothic";
        }

        // 히라가나/가타카나/CJK 통합 한자 (일본어/한자 위주)
        if ((codePoint >= 0x3040 && codePoint <= 0x30FF) ||
            (codePoint >= 0x3400 && codePoint <= 0x9FFF) ||
            (codePoint >= 0xF900 && codePoint <= 0xFAFF) ||
            (codePoint >= 0xFF00 && codePoint <= 0xFFEF))
        {
            return L"Yu Gothic";
        }

        if (codePoint >= 0x2000 && codePoint <= 0x27BF)
        {
            return L"Segoe UI Symbol"; // 일반 기호/구두점류 보강
        }

        return nullptr;
    }
}
