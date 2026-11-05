#pragma once
#include <string>
#include <vector>

namespace Cutty
{
    // 연결별 인코딩 지정값. 0 = 자동 감지, 그 외는 Windows 코드페이지 번호.
    constexpr unsigned kCodePageAuto = 0;
    constexpr unsigned kCodePageUtf8 = 65001;

    // 네트워크에서 받은 바이트 스트림을 UTF-32 코드포인트 목록으로 디코딩하고,
    // 키보드 입력을 같은 인코딩의 바이트로 인코딩한다.
    // 자동 감지 모드에서는 UTF-8 로 유효하게 해석되면 UTF-8 로, 아니면 시스템 ANSI
    // 코드페이지(한글 Windows 에서는 CP949)로 간주한다. 여러 조각(chunk)에 걸쳐 잘린
    // 멀티바이트 시퀀스(UTF-8 및 CP949 같은 DBCS)는 다음 호출까지 보관한다.
    class StreamDecoder
    {
    public:
        // kCodePageAuto(자동), kCodePageUtf8, 또는 949/932/936/950/1252/437 등 고정 코드페이지.
        void SetCodePage(unsigned codePage) { mCodePage = codePage; mPending.clear(); }
        unsigned CodePage() const { return mCodePage; }

        // 새로 도착한 바이트를 디코딩해 완성된 코드포인트들을 반환한다.
        // 시퀀스 중간에 잘린 바이트는 내부에 보관했다가 다음 호출에 이어붙인다.
        std::vector<char32_t> Decode(const uint8_t* data, size_t length);

        // 지금 실제로 쓰고 있는 코드페이지 (자동 모드면 감지 결과).
        unsigned EffectiveCodePage() const;
        // 키 입력/붙여넣기 텍스트를 원격으로 보낼 바이트로 변환한다 (수신과 같은 인코딩).
        std::string Encode(const std::wstring& text) const;

    private:
        unsigned mCodePage = kCodePageAuto;
        std::vector<uint8_t> mPending;      // 이전 호출에서 미처 완성되지 못한 바이트
        bool mConfirmedAnsi = false;        // (자동 모드) 유효하지 않은 UTF-8 시퀀스를 한 번이라도 만나면
                                             // true 로 고정하고 이후 영구적으로 ANSI 로 디코딩한다.
                                             // (순수 ASCII 청크만으로는 UTF-8 여부를 판단할 수 없으므로
                                             //  섣불리 ANSI 로 확정하지 않는다 - 그래야 배너 등 ASCII
                                             //  텍스트 뒤에 오는 한글/일본어 UTF-8 문자도 정상 디코딩됨.)

        static bool LooksLikeValidUtf8(const std::vector<uint8_t>& bytes);
        std::vector<char32_t> DecodeUtf8(const std::vector<uint8_t>& bytes, std::vector<uint8_t>& leftover);
        std::vector<char32_t> DecodeAnsi(const std::vector<uint8_t>& bytes, unsigned codePage, std::vector<uint8_t>& leftover);
    };
}
