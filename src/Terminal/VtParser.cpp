#include "VtParser.h"
#include "AnsiColors.h"
#include <algorithm>
#include <cstdio>

namespace Cutty
{
    namespace
    {
        constexpr char32_t kEsc = 0x1B;
        constexpr char32_t kBel = 0x07;
    }

    void VtParser::Feed(const std::vector<char32_t>& codePoints)
    {
        for (char32_t c : codePoints) ProcessChar(c);
    }

    int VtParser::Param(size_t index, int defaultValue) const
    {
        if (index >= mParams.size()) return defaultValue;
        int v = mParams[index];
        return v == 0 && defaultValue != 0 ? defaultValue : v;
    }

    bool VtParser::ExecuteControl(char32_t c)
    {
        switch (c)
        {
        case U'\n': case 0x0B: case 0x0C: mBuffer.LineFeed(); return true; // LF/VT/FF
        case U'\r': mBuffer.CarriageReturn(); return true;
        case U'\b': mBuffer.Backspace(); return true;
        case U'\t': mBuffer.Tab(); return true;
        case kBel: if (OnBell) OnBell(); return true;
        case 0x0E: mUsingG1 = true; return true;  // SO: G1 로 전환
        case 0x0F: mUsingG1 = false; return true; // SI: G0 로 전환
        default: return c < 0x20; // 나머지 C0 제어문자는 무시
        }
    }

    void VtParser::ProcessChar(char32_t c)
    {
        switch (mState)
        {
        case State::Ground:
            if (c == kEsc) { mState = State::Escape; return; }
            if (ExecuteControl(c)) return;
            if (c == 0x7F) return; // DEL 은 출력하지 않음
            {
                bool graphicsActive = mUsingG1 ? mG1IsGraphics : mG0IsGraphics;
                mLastPrinted = graphicsActive ? TranslateDecGraphics(c) : c;
                mBuffer.PutChar(mLastPrinted);
            }
            return;

        case State::Escape:
            DispatchEscape(c);
            return;

        case State::CsiParam:
            if ((c >= U'0' && c <= U'9') || c == U';' || c == U':')
            {
                mParamText.push_back(static_cast<wchar_t>(c));
                return;
            }
            if (c == U'?' || c == U'>' || c == U'=' || c == U'<')
            {
                if (mParamText.empty() && mIntermediate.empty()) mPrefix = static_cast<wchar_t>(c);
                return;
            }
            if (c >= 0x20 && c <= 0x2F) // 중간 바이트 (공백, '!', '"', '$' 등)
            {
                mIntermediate.push_back(static_cast<wchar_t>(c));
                return;
            }
            if (c == kEsc) { mState = State::Escape; return; } // 시퀀스 중단 후 새 ESC
            if (c < 0x20) { ExecuteControl(c); return; }      // CSI 도중의 제어문자는 즉시 실행
            ParseCsiParams();
            mState = State::Ground;
            DispatchCsi(c);
            return;

        case State::OscString:
        case State::IgnoreString:
            // 종료: BEL 또는 ST(ESC \). ESC 다음이 '\\' 가 아니면 새 이스케이프 시퀀스로 본다.
            if (mStringEscPending)
            {
                mStringEscPending = false;
                if (mState == State::OscString) FinishOsc();
                mState = State::Ground;
                if (c != U'\\') { mState = State::Escape; DispatchEscape(c); }
                return;
            }
            if (c == kEsc) { mStringEscPending = true; return; }
            if (c == kBel)
            {
                if (mState == State::OscString) FinishOsc();
                mState = State::Ground;
                return;
            }
            if (mState == State::OscString && mOscText.size() < 4096) mOscText.push_back(static_cast<wchar_t>(c));
            return;

        case State::Charset:
        {
            // '0' = DEC Special Graphics(박스 그리기 문자), 그 외(B/A/... )는 일반 ASCII 로 취급.
            bool isGraphics = (c == U'0');
            if (mPendingCharsetSlot == 0) mG0IsGraphics = isGraphics;
            else if (mPendingCharsetSlot == 1) mG1IsGraphics = isGraphics;
            // slot == -1 (G2/G3, ESC #, ESC % 등) 은 글자만 소비하고 무시
            mState = State::Ground;
            return;
        }
        }
    }

    void VtParser::FinishOsc()
    {
        // OSC 0/2: 창 제목 설정 "ESC ] 0;title BEL"
        size_t sep = mOscText.find(L';');
        if (sep == std::wstring::npos) return;
        std::wstring code = mOscText.substr(0, sep);
        if ((code == L"0" || code == L"2") && OnTitleChanged) OnTitleChanged(mOscText.substr(sep + 1));
    }

    void VtParser::DispatchEscape(char32_t c)
    {
        switch (c)
        {
        case U'[':
            mState = State::CsiParam;
            mParamText.clear();
            mIntermediate.clear();
            mPrefix = 0;
            return;
        case U']':
            mState = State::OscString;
            mOscText.clear();
            mStringEscPending = false;
            return;
        case U'P': case U'X': case U'^': case U'_': // DCS/SOS/PM/APC 문자열은 무시
            mState = State::IgnoreString;
            mStringEscPending = false;
            return;
        case U'(': mPendingCharsetSlot = 0; mState = State::Charset; return; // G0 지정
        case U')': mPendingCharsetSlot = 1; mState = State::Charset; return; // G1 지정
        case U'*': case U'+': case U'#': case U'%': case U' ':
            mPendingCharsetSlot = -1; // G2/G3, DECALN 등: 다음 한 글자만 소비하고 무시
            mState = State::Charset;
            return;
        case U'7': mBuffer.SaveCursor(); break;
        case U'8': mBuffer.RestoreCursor(); break;
        case U'D': mBuffer.LineFeed(); break;                 // Index
        case U'M': mBuffer.ScrollDown(1); break;               // Reverse Index
        case U'E': mBuffer.CarriageReturn(); mBuffer.LineFeed(); break; // Next Line
        case U'H': mBuffer.SetTabStop(); break;                // HTS
        case U'c': // RIS: 전체 초기화
            mBuffer.Reset(true);
            mApplicationCursorKeys = false;
            mBracketedPaste = false;
            mG0IsGraphics = mG1IsGraphics = mUsingG1 = false;
            break;
        default: break; // ESC = / ESC > (키패드 모드), ESC \ (ST) 등은 무시
        }
        mState = State::Ground;
    }

    // VT100 DEC Special Graphics(박스 그리기) 문자 매핑. top/htop/vim 등 ncurses
    // 프로그램이 ESC(0 로 이 문자셋을 지정한 뒤 0x5F~0x7E 범위 바이트로 테두리를
    // 그리는데, 이 변환이 없으면 화면에 엉뚱한 알파벳이 나와 정렬이 깨져 보인다.
    char32_t VtParser::TranslateDecGraphics(char32_t c)
    {
        switch (c)
        {
        case U'_': return U' ';
        case U'`': return 0x25C6; // ◆
        case U'a': return 0x2592; // ▒
        case U'b': return 0x2409; // ␉
        case U'c': return 0x240C; // ␌
        case U'd': return 0x240D; // ␍
        case U'e': return 0x240A; // ␊
        case U'f': return 0x00B0; // °
        case U'g': return 0x00B1; // ±
        case U'h': return 0x2424; // ␤
        case U'i': return 0x240B; // ␋
        case U'j': return 0x2518; // ┘
        case U'k': return 0x2510; // ┐
        case U'l': return 0x250C; // ┌
        case U'm': return 0x2514; // └
        case U'n': return 0x253C; // ┼
        case U'o': return 0x23BA; // ⎺
        case U'p': return 0x23BB; // ⎻
        case U'q': return 0x2500; // ─
        case U'r': return 0x23BC; // ⎼
        case U's': return 0x23BD; // ⎽
        case U't': return 0x251C; // ├
        case U'u': return 0x2524; // ┤
        case U'v': return 0x2534; // ┴
        case U'w': return 0x252C; // ┬
        case U'x': return 0x2502; // │
        case U'y': return 0x2264; // ≤
        case U'z': return 0x2265; // ≥
        case U'{': return 0x03C0; // π
        case U'|': return 0x2260; // ≠
        case U'}': return 0x00A3; // £
        case U'~': return 0x00B7; // ·
        default: return c;
        }
    }

    void VtParser::ParseCsiParams()
    {
        // ';' 는 파라미터 구분, ':' 는 하위 파라미터 구분(SGR 38:2:r:g:b, 4:3 등).
        // 지나치게 큰 숫자도 예외 없이 처리되도록 직접 파싱해 상한을 둔다.
        mParams.clear();
        mParamIsSub.clear();
        if (mParamText.empty()) return;

        int value = 0;
        bool isSub = false;
        for (wchar_t ch : mParamText)
        {
            if (ch >= L'0' && ch <= L'9')
            {
                value = std::min(value * 10 + (ch - L'0'), 99999);
                continue;
            }
            mParams.push_back(value);
            mParamIsSub.push_back(isSub);
            value = 0;
            isSub = (ch == L':');
        }
        mParams.push_back(value);
        mParamIsSub.push_back(isSub);
    }

    void VtParser::DispatchSgr()
    {
        if (mParams.empty()) { mBuffer.ResetAttr(); return; }
        GraphicState& attr = mBuffer.CurrentAttr();
        const GraphicState& defaults = mBuffer.DefaultAttr();

        // 38/48 의 확장 색 (5;n / 2;r;g;b, 또는 ':' 하위 파라미터 형식) 을 읽는다.
        auto readExtendedColor = [&](size_t& i, uint32_t& target)
        {
            std::vector<int> args;
            bool colonForm = (i + 1 < mParams.size() && mParamIsSub[i + 1]);
            size_t j = i + 1;
            while (j < mParams.size() && (!colonForm || mParamIsSub[j]) && args.size() < 6)
            {
                args.push_back(mParams[j]);
                ++j;
                if (colonForm) continue;
                // ';' 형식은 5;n 이면 2개, 2;r;g;b 면 4개만 소비 (그 뒤는 다음 SGR 파라미터)
                if ((args[0] != 5 && args[0] != 2) || (args[0] == 5 && args.size() == 2) || (args[0] == 2 && args.size() == 4)) break;
            }
            if (args.size() >= 2 && args[0] == 5)
            {
                target = GetAnsi256Color(args[1]);
            }
            else if (args.size() >= 4 && args[0] == 2)
            {
                // ':' 형식은 색공간 ID 가 끼어 있을 수 있음 (38:2:id:r:g:b)
                size_t base = (colonForm && args.size() >= 5) ? 2 : 1;
                target = (static_cast<uint32_t>(std::clamp(args[base], 0, 255)) << 16) |
                         (static_cast<uint32_t>(std::clamp(args[base + 1], 0, 255)) << 8) |
                         static_cast<uint32_t>(std::clamp(args[base + 2], 0, 255));
            }
            i = j - 1;
        };

        for (size_t i = 0; i < mParams.size(); ++i)
        {
            if (mParamIsSub[i]) continue; // 처리하지 않는 하위 파라미터는 건너뜀
            int p = mParams[i];
            bool hasSub = (i + 1 < mParams.size() && mParamIsSub[i + 1]);

            if (p == 0) attr = defaults;
            else if (p == 1) attr.bold = true;
            else if (p == 2) attr.dim = true;
            else if (p == 3) attr.italic = true;
            else if (p == 4) attr.underline = hasSub ? mParams[i + 1] != 0 : true; // 4:0 = 밑줄 없음
            else if (p == 7) attr.reverse = true;
            else if (p == 8) attr.hidden = true;
            else if (p == 9) attr.strikethrough = true;
            else if (p == 21) attr.underline = true; // 이중 밑줄 -> 밑줄로 표시
            else if (p == 22) { attr.bold = false; attr.dim = false; }
            else if (p == 23) attr.italic = false;
            else if (p == 24) attr.underline = false;
            else if (p == 27) attr.reverse = false;
            else if (p == 28) attr.hidden = false;
            else if (p == 29) attr.strikethrough = false;
            else if (p >= 30 && p <= 37) attr.foreground = GetAnsi16Color(p - 30);
            else if (p == 38) readExtendedColor(i, attr.foreground);
            else if (p == 39) attr.foreground = defaults.foreground;
            else if (p >= 40 && p <= 47) attr.background = GetAnsi16Color(p - 40);
            else if (p == 48) readExtendedColor(i, attr.background);
            else if (p == 49) attr.background = defaults.background;
            else if (p == 58) { uint32_t ignored = 0; readExtendedColor(i, ignored); } // 밑줄 색: 미지원, 인자만 소비
            else if (p >= 90 && p <= 97) attr.foreground = GetAnsi16Color(p - 90 + 8);
            else if (p >= 100 && p <= 107) attr.background = GetAnsi16Color(p - 100 + 8);
            // 5/6(깜빡임), 25 등은 무시
        }
    }

    void VtParser::DispatchPrivateMode(int mode, bool enable)
    {
        switch (mode)
        {
        case 1: mApplicationCursorKeys = enable; break; // DECCKM
        case 6: mBuffer.SetOriginMode(enable); break;    // DECOM
        case 7: mBuffer.SetAutoWrap(enable); break;      // DECAWM
        case 25: mBuffer.SetCursorVisible(enable); break; // DECTCEM
        case 47: case 1047: // 대체 화면만 (커서 저장/복원 없음)
            if (enable) mBuffer.EnterAlternateScreen();
            else mBuffer.LeaveAlternateScreen();
            break;
        case 1048: // 커서 저장/복원만
            if (enable) mBuffer.SaveCursor();
            else mBuffer.RestoreCursor();
            break;
        case 1049: // 대체 화면 + 커서 저장/복원 (top/htop/vim/less 등이 사용)
            if (enable) { mBuffer.SaveCursor(); mBuffer.EnterAlternateScreen(); }
            else { mBuffer.LeaveAlternateScreen(); mBuffer.RestoreCursor(); }
            break;
        case 2004: mBracketedPaste = enable; break;
        default: break; // 12(커서 깜빡임), 1000~1006(마우스 보고) 등은 미지원
        }
    }

    void VtParser::DispatchCsi(char32_t finalByte)
    {
        // 중간 바이트가 붙은 시퀀스: 필요한 것만 처리하고 나머지는 조용히 무시한다
        // (예전에는 공백을 최종 바이트로 오인해 'CSI 2 SP q' 의 'q' 가 화면에 찍혔다).
        if (!mIntermediate.empty())
        {
            if (mIntermediate == L"!" && finalByte == U'p') // DECSTR: 소프트 리셋
            {
                mBuffer.Reset(false);
                mApplicationCursorKeys = false;
                mBracketedPaste = false;
            }
            return; // CSI Ps SP q (커서 모양) 등은 무시
        }

        if (mPrefix == L'>')
        {
            // Secondary DA: xterm 처럼 응답. 'CSI > 4;2 m'(modifyOtherKeys) 같은 것을
            // SGR 로 오인하면 밑줄이 켜지는 등 화면이 깨지므로 나머지는 무시한다.
            if (finalByte == U'c' && OnResponse) OnResponse("\x1b[>0;10;1c");
            return;
        }
        if (mPrefix == L'=' || mPrefix == L'<') return;

        if (mPrefix == L'?')
        {
            if (finalByte == U'h' || finalByte == U'l')
            {
                for (int mode : mParams) DispatchPrivateMode(mode, finalByte == U'h');
            }
            else if (finalByte == U'n' && Param(0, 0) == 6 && OnResponse) // DECXCPR
            {
                char buf[40];
                sprintf_s(buf, "\x1b[?%d;%dR", mBuffer.CursorRow() + 1, mBuffer.CursorColumn() + 1);
                OnResponse(buf);
            }
            return;
        }

        switch (finalByte)
        {
        case U'A': mBuffer.MoveCursorRelative(-Param(0, 1), 0); break;
        case U'B': case U'e': mBuffer.MoveCursorRelative(Param(0, 1), 0); break;   // CUD / VPR
        case U'C': case U'a': mBuffer.MoveCursorRelative(0, Param(0, 1)); break;   // CUF / HPR
        case U'D': mBuffer.MoveCursorRelative(0, -Param(0, 1)); break;
        case U'E': mBuffer.MoveCursorRelative(Param(0, 1), 0); mBuffer.CarriageReturn(); break; // CNL
        case U'F': mBuffer.MoveCursorRelative(-Param(0, 1), 0); mBuffer.CarriageReturn(); break; // CPL
        case U'G': case U'`': mBuffer.SetCursorColumn(Param(0, 1) - 1); break;      // CHA / HPA
        case U'd': mBuffer.SetCursorRow(Param(0, 1) - 1); break;                    // VPA
        case U'H': case U'f':
            mBuffer.MoveCursorToOrigin(Param(0, 1) - 1, Param(1, 1) - 1);
            break;
        case U'I': mBuffer.Tab(Param(0, 1)); break;      // CHT
        case U'Z': mBuffer.BackTab(Param(0, 1)); break;  // CBT
        case U'J': mBuffer.EraseInDisplay(Param(0, 0)); break;
        case U'K': mBuffer.EraseInLine(Param(0, 0)); break;
        case U'X': mBuffer.EraseChars(Param(0, 1)); break; // ECH
        case U'L': mBuffer.InsertLines(Param(0, 1)); break;
        case U'M': mBuffer.DeleteLines(Param(0, 1)); break;
        case U'P': mBuffer.DeleteChars(Param(0, 1)); break;
        case U'@': mBuffer.InsertChars(Param(0, 1)); break;
        case U'S': mBuffer.ScrollUp(Param(0, 1)); break;
        case U'T': mBuffer.ScrollDown(Param(0, 1)); break;
        case U'b': // REP: 직전 출력 문자를 반복 (ncurses 가 같은 글자가 이어질 때 사용)
            if (mLastPrinted)
            {
                int count = std::min(Param(0, 1), mBuffer.Columns() * mBuffer.Rows());
                for (int i = 0; i < count; ++i) mBuffer.PutChar(mLastPrinted);
            }
            break;
        case U'g': mBuffer.ClearTabStop(Param(0, 0)); break; // TBC
        case U'm': DispatchSgr(); break;
        case U'r':
            mBuffer.SetScrollRegion(Param(0, 1) - 1, Param(1, mBuffer.Rows()) - 1);
            break;
        case U's': mBuffer.SaveCursor(); break;
        case U'u': mBuffer.RestoreCursor(); break;
        case U'n':
            if (!OnResponse) break;
            if (Param(0, 0) == 5) OnResponse("\x1b[0n"); // 상태 보고: 정상
            else if (Param(0, 0) == 6)
            {
                char buf[32];
                sprintf_s(buf, "\x1b[%d;%dR", mBuffer.CursorRow() + 1, mBuffer.CursorColumn() + 1);
                OnResponse(buf);
            }
            break;
        case U'c':
            if (OnResponse) OnResponse("\x1b[?1;2c"); // Primary Device Attributes (VT100 계열 최소 응답)
            break;
        case U't': // 창 조작: 텍스트 영역 크기 질의(18)만 응답
            if (Param(0, 0) == 18 && OnResponse)
            {
                char buf[40];
                sprintf_s(buf, "\x1b[8;%d;%dt", mBuffer.Rows(), mBuffer.Columns());
                OnResponse(buf);
            }
            break;
        case U'h': case U'l':
            for (int mode : mParams)
            {
                if (mode == 4) mBuffer.SetInsertMode(finalByte == U'h'); // IRM
            }
            break;
        default: break;
        }
    }
}
