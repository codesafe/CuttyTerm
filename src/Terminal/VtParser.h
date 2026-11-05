#pragma once
#include "TerminalBuffer.h"
#include <vector>
#include <string>
#include <functional>

namespace Cutty
{
    // VT100/xterm 서브셋 이스케이프 시퀀스 파서.
    // 바이트 스트림(코드포인트 단위)을 상태 기계로 해석해 TerminalBuffer 를 갱신한다.
    class VtParser
    {
    public:
        explicit VtParser(TerminalBuffer& buffer) : mBuffer(buffer) {}

        void Feed(const std::vector<char32_t>& codePoints);

        // DECCKM(CSI ?1h/l): 켜져 있으면 방향키/Home/End 를 ESC O x 형식으로 보내야 한다
        // (zsh/vim/less 등이 켜고, 이 형식만 인식함).
        bool ApplicationCursorKeys() const { return mApplicationCursorKeys; }
        // CSI ?2004h: 붙여넣기를 ESC[200~ ... ESC[201~ 로 감싸 보내야 한다 (bash/zsh/vim 이 켬).
        bool BracketedPaste() const { return mBracketedPaste; }

        // 창 제목 변경(OSC 0/2), 벨(BEL), 호스트로 되돌려줄 응답(DSR/DA 등)을 위한 콜백.
        std::function<void(const std::wstring&)> OnTitleChanged;
        std::function<void()> OnBell;
        std::function<void(const std::string&)> OnResponse; // 파서가 host 로 보내야 하는 바이트

    private:
        enum class State
        {
            Ground,
            Escape,
            CsiParam,
            OscString,
            IgnoreString, // DCS/SOS/PM/APC: 종료(ST/BEL)까지 내용을 버림
            Charset,      // ESC ( / ESC ) / ESC # 등 다음 한 글자 소비용
        };

        void ProcessChar(char32_t c);
        bool ExecuteControl(char32_t c); // C0 제어문자 처리, 처리했으면 true
        void FinishOsc();
        void ParseCsiParams();
        void DispatchCsi(char32_t finalByte);
        void DispatchPrivateMode(int mode, bool enable);
        void DispatchSgr();
        void DispatchEscape(char32_t c);
        int Param(size_t index, int defaultValue) const;
        static char32_t TranslateDecGraphics(char32_t c);

        TerminalBuffer& mBuffer;
        State mState = State::Ground;
        std::wstring mParamText;     // CSI 파라미터 원문 (숫자, ';', ':')
        wchar_t mPrefix = 0;         // CSI 직후의 '?', '>', '=', '<' (사설 시퀀스 구분)
        std::wstring mIntermediate;  // 최종 바이트 앞의 중간 바이트 (예: CSI 2 SP q, CSI ! p)
        std::wstring mOscText;
        bool mStringEscPending = false; // OSC/DCS 안에서 ESC 를 받음 (다음이 '\\' 면 ST)
        std::vector<int> mParams;
        std::vector<bool> mParamIsSub;  // ':' 로 이어진 하위 파라미터인지 (SGR 38:2:r:g:b 등)
        char32_t mLastPrinted = 0;      // REP(CSI b) 가 반복할 마지막 출력 문자

        // G0/G1 문자셋 지정 및 SO/SI 전환 상태 (top/htop/vim 등이 쓰는 VT100
        // 박스 그리기 문자(DEC Special Graphics)를 올바르게 표시하기 위함).
        int mPendingCharsetSlot = 0;  // ESC ( 이면 0(G0), ESC ) 이면 1(G1), 그 외 -1
        bool mG0IsGraphics = false;
        bool mG1IsGraphics = false;
        bool mUsingG1 = false;        // SO(0x0E)/SI(0x0F) 로 전환되는 현재 활성 G-set

        bool mApplicationCursorKeys = false;
        bool mBracketedPaste = false;
    };
}
