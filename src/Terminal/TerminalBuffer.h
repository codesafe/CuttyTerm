#pragma once
#include "Cell.h"
#include <vector>
#include <deque>
#include <string>
#include <cstdint>

namespace Cutty
{
    // 터미널 화면 그리드 + 스크롤백 버퍼. VtParser 가 파싱한 명령을 실행하는 대상이며,
    // TextRenderer 가 이 버퍼를 읽어 화면에 그린다.
    class TerminalBuffer
    {
    public:
        void Resize(int columns, int rows);

        int Columns() const { return mColumns; }               // 현재 "보이는/원격에 알려준" 폭
        int LogicalColumns() const { return mLogicalColumns; }  // 실제 셀 저장 폭(가로 스크롤용, >= Columns())
        int Rows() const { return mRows; }
        int CursorRow() const { return mCursorRow; }
        int CursorColumn() const { return mCursorColumn; }
        bool IsCursorVisible() const { return mCursorVisible; }

        const Cell& At(int row, int column) const;

        // 스크롤백 포함 특정 뷰포트 라인 접근 (renderer 용). offsetFromBottom=0 이면 화면 맨 아래.
        const std::vector<Cell>& ViewLine(int viewportRowFromTop, int scrollOffset) const;
        int TotalScrollableLines() const; // 스크롤백 + 화면

        // 줄 ID: 스크롤백 맨 앞 줄이 잘려 나가도 바뀌지 않는 절대 줄 번호 (마우스 선택 범위 유지용).
        int64_t ViewLineId(int viewportRowFromTop, int scrollOffset) const;
        const std::vector<Cell>* LineById(int64_t lineId) const; // 이미 버려진 줄이면 nullptr

        // ---- 문자 출력 ----
        void PutChar(char32_t codePoint);

        // ---- 커서/개행 제어 ----
        void LineFeed();
        void CarriageReturn();
        void Backspace();
        void Tab(int count = 1);       // 다음 탭 정지점으로 (CHT)
        void BackTab(int count = 1);   // 이전 탭 정지점으로 (CBT)
        void SetTabStop();             // 현재 컬럼에 탭 정지점 (HTS)
        void ClearTabStop(int mode);   // 0: 현재 컬럼, 3: 전부 (TBC)
        void MoveCursorTo(int row, int column); // 0-based, 화면 좌표로 clamp
        void MoveCursorToOrigin(int row, int column); // CUP/HVP: origin 모드면 스크롤 영역 기준
        void SetCursorColumn(int column);  // CHA/HPA
        void SetCursorRow(int row);        // VPA (origin 모드 반영)
        void MoveCursorRelative(int deltaRow, int deltaColumn);
        void SaveCursor();
        void RestoreCursor();
        void SetCursorVisible(bool visible) { mCursorVisible = visible; }

        // ---- 지우기 ----
        void EraseInLine(int mode);    // 0: 커서~끝, 1: 처음~커서, 2: 전체
        void EraseInDisplay(int mode); // 0: 커서~끝, 1: 처음~커서, 2: 전체, 3: 스크롤백 포함 전체
        void EraseChars(int count);    // ECH: 커서 위치부터 count 칸을 지움 (커서는 그대로)

        // ---- 라인/문자 삽입-삭제 ----
        void InsertChars(int count);
        void DeleteChars(int count);
        void InsertLines(int count);
        void DeleteLines(int count);

        // ---- 스크롤 영역 ----
        void SetScrollRegion(int top, int bottom); // 0-based, inclusive
        void ScrollUp(int count);
        void ScrollDown(int count);

        // ---- 속성 ----
        GraphicState& CurrentAttr() { return mCurrentAttr; }
        void ResetAttr() { mCurrentAttr = mDefaultAttr; }
        // SGR 0/39/49 가 돌아갈 "기본" 글자/배경색 (옵션창에서 설정한 색).
        const GraphicState& DefaultAttr() const { return mDefaultAttr; }
        void SetDefaultColors(uint32_t foreground, uint32_t background)
        {
            mDefaultAttr.foreground = foreground;
            mDefaultAttr.background = background;
        }

        // ---- 렌더러용 dirty 추적 ----
        bool IsRowDirty(int row) const;
        void ClearDirty();
        void MarkAllDirty();

        void SetAutoWrap(bool enabled) { mAutoWrapEnabled = enabled; }
        void SetOriginMode(bool enabled) { mOriginMode = enabled; MoveCursorToOrigin(0, 0); }
        void SetInsertMode(bool enabled) { mInsertMode = enabled; } // IRM (CSI 4h/l)

        // RIS(ESC c)/DECSTR(CSI ! p): 화면/모드/탭 정지점/스크롤 영역을 초기 상태로.
        void Reset(bool clearScreen);

        // ---- 대체 화면 버퍼 (top/htop/vim/less 등 풀스크린 프로그램용) ----
        // 진입 시 현재 화면을 보관하고 빈 화면으로 시작하며, 스크롤백에는 쌓이지 않는다.
        // 나갈 때 원래 화면으로 복원한다.
        void EnterAlternateScreen();
        void LeaveAlternateScreen();
        bool IsAlternateScreen() const { return mUsingAltScreen; }

        // 배경/글자색을 바꿀 때, 예전 기본색과 정확히 일치하는 셀들을 새 기본색으로
        // 일괄 치환한다(옵션창에서 색을 바꾸면 이미 표시된 내용도 반영되도록).
        void RecolorDefaults(uint32_t oldForeground, uint32_t newForeground,
                              uint32_t oldBackground, uint32_t newBackground);

    private:
        void MarkDirty(int row);
        void NewLineAt(int row); // 스크롤 영역 하단에서 한 줄 밀어냄 (스크롤백에 저장)
        std::vector<Cell> MakeBlankRow() const;
        void EnsureTabStops(); // 폭이 늘어난 만큼 기본 탭 정지점(8칸마다)을 채움

        int mColumns = 80;        // 뷰포트(원격에 알려주는) 폭
        int mLogicalColumns = 80; // 셀 저장 폭 - 창이 넓어졌던 최대치를 기억해 좁혀도 데이터 유실 없음
        int mRows = 24;
        std::vector<std::vector<Cell>> mScreen;
        std::deque<std::vector<Cell>> mScrollback;
        static constexpr size_t kMaxScrollback = 5000;
        int64_t mDiscardedLines = 0; // 스크롤백 한도 초과/삭제로 버려진 줄 수 (줄 ID 기준점)

        int mCursorRow = 0;
        int mCursorColumn = 0;
        bool mCursorVisible = true;
        bool mWrapPending = false; // 마지막 컬럼에 쓴 뒤 다음 출력 시 줄바꿈 예약
        bool mAutoWrapEnabled = true;
        bool mOriginMode = false;
        bool mInsertMode = false;
        std::vector<bool> mTabStops; // mLogicalColumns 크기, 기본은 8칸마다

        int mSavedCursorRow = 0;
        int mSavedCursorColumn = 0;
        GraphicState mSavedAttr;

        int mScrollTop = 0;
        int mScrollBottom = 23;

        GraphicState mCurrentAttr;
        GraphicState mDefaultAttr;
        std::vector<bool> mDirtyRows;

        // 대체 화면 버퍼 상태
        std::vector<std::vector<Cell>> mMainScreen; // 대체 화면 진입 시 메인 화면 백업
        bool mUsingAltScreen = false;
        int mMainCursorRow = 0;
        int mMainCursorColumn = 0;
    };
}
