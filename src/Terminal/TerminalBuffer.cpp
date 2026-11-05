#include "TerminalBuffer.h"
#include "CharWidth.h"
#include <algorithm>

namespace Cutty
{
    std::vector<Cell> TerminalBuffer::MakeBlankRow() const
    {
        Cell blank;
        blank.foreground = mCurrentAttr.foreground;
        blank.background = mCurrentAttr.background;
        return std::vector<Cell>(mLogicalColumns, blank);
    }

    void TerminalBuffer::Resize(int columns, int rows)
    {
        columns = std::max(1, columns);
        rows = std::max(1, rows);
        if (columns == mColumns && rows == mRows) return;

        // 실제 셀 저장 폭(mLogicalColumns)은 지금까지 창이 가장 넓었던 폭보다
        // 절대 줄어들지 않는다 - 창을 좁혀도 오른쪽으로 밀려난 기존 글자가
        // 사라지지 않고 가로 스크롤로 다시 볼 수 있게 하기 위함.
        int newLogicalColumns = std::max(mLogicalColumns, columns);

        Cell blank;
        blank.foreground = mCurrentAttr.foreground;
        blank.background = mCurrentAttr.background;

        // 창 크기가 바뀌어도 기존에 출력된 내용이 통째로 사라지지 않도록,
        // 새 크기의 빈 그리드를 만들고 이전 내용을 (좌상단 기준으로) 옮겨 담는다.
        // 완전한 재줄바꿈(reflow)은 아니지만 최소한 화면/글자가 없어지는 건 막아준다.
        auto resizeGrid = [&](std::vector<std::vector<Cell>>& grid, int& cursorRow, int& cursorColumn)
        {
            std::vector<std::vector<Cell>> newGrid(rows, std::vector<Cell>(newLogicalColumns, blank));
            int copyRows = std::min(static_cast<int>(grid.size()), rows);
            for (int r = 0; r < copyRows; ++r)
            {
                int copyCols = std::min(static_cast<int>(grid[r].size()), newLogicalColumns);
                for (int c = 0; c < copyCols; ++c) newGrid[r][c] = grid[r][c];
            }
            grid = std::move(newGrid);
            cursorRow = std::min(cursorRow, rows - 1);
            cursorColumn = std::min(cursorColumn, columns - 1);
        };

        resizeGrid(mScreen, mCursorRow, mCursorColumn);

        // 대체 화면으로 전환 중 창 크기가 바뀌면, 나중에 복원할 메인 화면도
        // 같은 방식으로 내용을 보존하며 리사이즈한다 (그렇지 않으면 복귀 시
        // 크기가 어긋나거나 내용이 사라진다).
        if (mUsingAltScreen && !mMainScreen.empty())
        {
            resizeGrid(mMainScreen, mMainCursorRow, mMainCursorColumn);
        }

        mColumns = columns;
        mLogicalColumns = newLogicalColumns;
        mRows = rows;
        mScrollTop = 0;
        mScrollBottom = mRows - 1;
        mDirtyRows.assign(mRows, true);
    }

    void TerminalBuffer::RecolorDefaults(uint32_t oldForeground, uint32_t newForeground,
                                          uint32_t oldBackground, uint32_t newBackground)
    {
        if (oldForeground == newForeground && oldBackground == newBackground) return;

        auto recolorRow = [&](std::vector<Cell>& row)
        {
            for (auto& cell : row)
            {
                if (cell.foreground == oldForeground) cell.foreground = newForeground;
                if (cell.background == oldBackground) cell.background = newBackground;
            }
        };

        for (auto& row : mScreen) recolorRow(row);
        for (auto& row : mMainScreen) recolorRow(row);
        for (auto& row : mScrollback) recolorRow(row);

        MarkAllDirty();
    }

    void TerminalBuffer::EnterAlternateScreen()
    {
        if (mUsingAltScreen) return;
        mMainScreen = mScreen;
        mMainCursorRow = mCursorRow;
        mMainCursorColumn = mCursorColumn;
        mScreen.assign(mRows, MakeBlankRow());
        mCursorRow = 0;
        mCursorColumn = 0;
        mScrollTop = 0;
        mScrollBottom = mRows - 1;
        mUsingAltScreen = true;
        MarkAllDirty();
    }

    void TerminalBuffer::LeaveAlternateScreen()
    {
        if (!mUsingAltScreen) return;
        mScreen = mMainScreen;
        mMainScreen.clear();
        mCursorRow = mMainCursorRow;
        mCursorColumn = mMainCursorColumn;
        mScrollTop = 0;
        mScrollBottom = mRows - 1;
        mUsingAltScreen = false;
        MarkAllDirty();
    }

    const Cell& TerminalBuffer::At(int row, int column) const
    {
        static Cell fallback;
        if (row < 0 || row >= mRows || column < 0 || column >= mColumns) return fallback;
        return mScreen[row][column];
    }

    int TerminalBuffer::TotalScrollableLines() const
    {
        return static_cast<int>(mScrollback.size()) + mRows;
    }

    const std::vector<Cell>& TerminalBuffer::ViewLine(int viewportRowFromTop, int scrollOffset) const
    {
        // scrollOffset: 0 = 맨 아래(최신), 양수면 과거로 스크롤한 만큼
        static std::vector<Cell> empty;
        int totalLines = TotalScrollableLines();
        int lastVisibleTop = std::max(0, totalLines - mRows - scrollOffset);
        int lineIndex = lastVisibleTop + viewportRowFromTop;

        if (lineIndex < 0 || lineIndex >= totalLines) return empty;

        int scrollbackCount = static_cast<int>(mScrollback.size());
        if (lineIndex < scrollbackCount) return mScrollback[lineIndex];
        return mScreen[lineIndex - scrollbackCount];
    }

    int64_t TerminalBuffer::ViewLineId(int viewportRowFromTop, int scrollOffset) const
    {
        int lastVisibleTop = std::max(0, TotalScrollableLines() - mRows - scrollOffset);
        return mDiscardedLines + lastVisibleTop + viewportRowFromTop;
    }

    const std::vector<Cell>* TerminalBuffer::LineById(int64_t lineId) const
    {
        int64_t index = lineId - mDiscardedLines;
        if (index < 0 || index >= TotalScrollableLines()) return nullptr;
        int scrollbackCount = static_cast<int>(mScrollback.size());
        if (index < scrollbackCount) return &mScrollback[static_cast<size_t>(index)];
        return &mScreen[static_cast<size_t>(index - scrollbackCount)];
    }

    void TerminalBuffer::MarkDirty(int row)
    {
        if (row >= 0 && row < static_cast<int>(mDirtyRows.size())) mDirtyRows[row] = true;
    }

    bool TerminalBuffer::IsRowDirty(int row) const
    {
        return row >= 0 && row < static_cast<int>(mDirtyRows.size()) && mDirtyRows[row];
    }

    void TerminalBuffer::ClearDirty()
    {
        std::fill(mDirtyRows.begin(), mDirtyRows.end(), false);
    }

    void TerminalBuffer::MarkAllDirty()
    {
        std::fill(mDirtyRows.begin(), mDirtyRows.end(), true);
    }

    void TerminalBuffer::PutChar(char32_t codePoint)
    {
        bool wide = IsWideChar(codePoint);
        int cellWidth = wide ? 2 : 1;

        if (mWrapPending)
        {
            mCursorColumn = 0;
            LineFeed();
            mWrapPending = false;
        }

        if (mCursorColumn + cellWidth > mColumns)
        {
            // 넓은 문자가 마지막 한 칸에 걸치면 줄바꿈 후 출력
            mCursorColumn = 0;
            LineFeed();
        }

        if (mInsertMode) InsertChars(cellWidth); // IRM: 기존 글자를 오른쪽으로 밀고 삽입

        Cell cell;
        cell.codepoint = codePoint;
        cell.foreground = mCurrentAttr.foreground;
        cell.background = mCurrentAttr.background;
        cell.bold = mCurrentAttr.bold;
        cell.underline = mCurrentAttr.underline;
        cell.reverse = mCurrentAttr.reverse;
        cell.dim = mCurrentAttr.dim;
        cell.italic = mCurrentAttr.italic;
        cell.strikethrough = mCurrentAttr.strikethrough;
        cell.hidden = mCurrentAttr.hidden;
        cell.isWide = wide;

        mScreen[mCursorRow][mCursorColumn] = cell;
        MarkDirty(mCursorRow);

        if (wide && mCursorColumn + 1 < mColumns)
        {
            Cell trailing;
            trailing.isTrailing = true;
            trailing.background = mCurrentAttr.background;
            mScreen[mCursorRow][mCursorColumn + 1] = trailing;
        }

        mCursorColumn += cellWidth;

        if (mCursorColumn >= mColumns)
        {
            if (mAutoWrapEnabled)
            {
                mCursorColumn = mColumns - 1;
                mWrapPending = true;
            }
            else
            {
                mCursorColumn = mColumns - 1;
            }
        }
    }

    void TerminalBuffer::NewLineAt(int /*row*/)
    {
        // 스크롤 영역 맨 위 줄을 스크롤백으로 밀어내고 새 빈 줄을 하단에 추가
        if (mScrollTop == 0)
        {
            mScrollback.push_back(mScreen[mScrollTop]);
            if (mScrollback.size() > kMaxScrollback)
            {
                mScrollback.pop_front();
                ++mDiscardedLines;
            }
        }

        for (int r = mScrollTop; r < mScrollBottom; ++r)
        {
            mScreen[r] = mScreen[r + 1];
        }
        mScreen[mScrollBottom] = MakeBlankRow();

        for (int r = mScrollTop; r <= mScrollBottom; ++r) MarkDirty(r);
    }

    void TerminalBuffer::LineFeed()
    {
        if (mCursorRow == mScrollBottom)
        {
            NewLineAt(mCursorRow);
        }
        else if (mCursorRow < mRows - 1)
        {
            ++mCursorRow;
        }
        mWrapPending = false;
    }

    void TerminalBuffer::CarriageReturn()
    {
        mCursorColumn = 0;
        mWrapPending = false;
    }

    void TerminalBuffer::Backspace()
    {
        if (mCursorColumn > 0) --mCursorColumn;
        mWrapPending = false;
    }

    void TerminalBuffer::EnsureTabStops()
    {
        size_t oldSize = mTabStops.size();
        if (oldSize >= static_cast<size_t>(mLogicalColumns)) return;
        mTabStops.resize(mLogicalColumns, false);
        for (size_t c = oldSize; c < mTabStops.size(); ++c) mTabStops[c] = (c % 8 == 0 && c != 0);
    }

    void TerminalBuffer::Tab(int count)
    {
        EnsureTabStops();
        for (int i = 0; i < count && mCursorColumn < mColumns - 1; ++i)
        {
            do { ++mCursorColumn; } while (mCursorColumn < mColumns - 1 && !mTabStops[mCursorColumn]);
        }
        mWrapPending = false;
    }

    void TerminalBuffer::BackTab(int count)
    {
        EnsureTabStops();
        for (int i = 0; i < count && mCursorColumn > 0; ++i)
        {
            do { --mCursorColumn; } while (mCursorColumn > 0 && !mTabStops[mCursorColumn]);
        }
        mWrapPending = false;
    }

    void TerminalBuffer::SetTabStop()
    {
        EnsureTabStops();
        mTabStops[mCursorColumn] = true;
    }

    void TerminalBuffer::ClearTabStop(int mode)
    {
        EnsureTabStops();
        if (mode == 0) mTabStops[mCursorColumn] = false;
        else if (mode == 3) std::fill(mTabStops.begin(), mTabStops.end(), false);
    }

    void TerminalBuffer::MoveCursorTo(int row, int column)
    {
        mCursorRow = std::clamp(row, 0, mRows - 1);
        mCursorColumn = std::clamp(column, 0, mColumns - 1);
        mWrapPending = false;
    }

    void TerminalBuffer::MoveCursorToOrigin(int row, int column)
    {
        if (mOriginMode)
        {
            mCursorRow = std::clamp(mScrollTop + row, mScrollTop, mScrollBottom);
            mCursorColumn = std::clamp(column, 0, mColumns - 1);
            mWrapPending = false;
            return;
        }
        MoveCursorTo(row, column);
    }

    void TerminalBuffer::SetCursorColumn(int column)
    {
        MoveCursorTo(mCursorRow, column);
    }

    void TerminalBuffer::SetCursorRow(int row)
    {
        MoveCursorToOrigin(row, mCursorColumn);
    }

    void TerminalBuffer::Reset(bool clearScreen)
    {
        mCurrentAttr = mDefaultAttr;
        mInsertMode = false;
        mOriginMode = false;
        mAutoWrapEnabled = true;
        mCursorVisible = true;
        mScrollTop = 0;
        mScrollBottom = mRows - 1;
        mTabStops.clear();
        EnsureTabStops();
        if (clearScreen)
        {
            if (mUsingAltScreen) LeaveAlternateScreen();
            EraseInDisplay(2);
            MoveCursorTo(0, 0);
        }
        mSavedCursorRow = 0;
        mSavedCursorColumn = 0;
        mSavedAttr = mDefaultAttr;
    }

    void TerminalBuffer::MoveCursorRelative(int deltaRow, int deltaColumn)
    {
        MoveCursorTo(mCursorRow + deltaRow, mCursorColumn + deltaColumn);
    }

    void TerminalBuffer::SaveCursor()
    {
        mSavedCursorRow = mCursorRow;
        mSavedCursorColumn = mCursorColumn;
        mSavedAttr = mCurrentAttr;
    }

    void TerminalBuffer::RestoreCursor()
    {
        mCursorRow = mSavedCursorRow;
        mCursorColumn = mSavedCursorColumn;
        mCurrentAttr = mSavedAttr;
        mWrapPending = false;
    }

    void TerminalBuffer::EraseInLine(int mode)
    {
        Cell blank;
        blank.foreground = mCurrentAttr.foreground;
        blank.background = mCurrentAttr.background;

        int from = 0, to = mColumns - 1;
        if (mode == 0) from = mCursorColumn;
        else if (mode == 1) to = mCursorColumn;
        // mode == 2 : 전체 (기본값 유지)

        for (int c = from; c <= to; ++c) mScreen[mCursorRow][c] = blank;
        MarkDirty(mCursorRow);
    }

    void TerminalBuffer::EraseInDisplay(int mode)
    {
        Cell blank;
        blank.foreground = mCurrentAttr.foreground;
        blank.background = mCurrentAttr.background;

        if (mode == 2 || mode == 3)
        {
            for (auto& row : mScreen) std::fill(row.begin(), row.end(), blank);
            if (mode == 3)
            {
                mDiscardedLines += static_cast<int64_t>(mScrollback.size());
                mScrollback.clear();
            }
            MarkAllDirty();
            return;
        }

        if (mode == 0)
        {
            EraseInLine(0);
            for (int r = mCursorRow + 1; r < mRows; ++r)
            {
                std::fill(mScreen[r].begin(), mScreen[r].end(), blank);
                MarkDirty(r);
            }
        }
        else if (mode == 1)
        {
            EraseInLine(1);
            for (int r = 0; r < mCursorRow; ++r)
            {
                std::fill(mScreen[r].begin(), mScreen[r].end(), blank);
                MarkDirty(r);
            }
        }
    }

    void TerminalBuffer::EraseChars(int count)
    {
        Cell blank;
        blank.foreground = mCurrentAttr.foreground;
        blank.background = mCurrentAttr.background;
        int to = std::min(mColumns, mCursorColumn + std::max(1, count));
        for (int c = mCursorColumn; c < to; ++c) mScreen[mCursorRow][c] = blank;
        MarkDirty(mCursorRow);
    }

    void TerminalBuffer::InsertChars(int count)
    {
        auto& row = mScreen[mCursorRow];
        Cell blank;
        blank.foreground = mCurrentAttr.foreground;
        blank.background = mCurrentAttr.background;

        count = std::min(count, mColumns - mCursorColumn);
        for (int i = mColumns - 1; i >= mCursorColumn + count; --i) row[i] = row[i - count];
        for (int i = mCursorColumn; i < mCursorColumn + count && i < mColumns; ++i) row[i] = blank;
        MarkDirty(mCursorRow);
    }

    void TerminalBuffer::DeleteChars(int count)
    {
        auto& row = mScreen[mCursorRow];
        Cell blank;
        blank.foreground = mCurrentAttr.foreground;
        blank.background = mCurrentAttr.background;

        count = std::min(count, mColumns - mCursorColumn);
        for (int i = mCursorColumn; i < mColumns - count; ++i) row[i] = row[i + count];
        for (int i = mColumns - count; i < mColumns; ++i) row[i] = blank;
        MarkDirty(mCursorRow);
    }

    void TerminalBuffer::InsertLines(int count)
    {
        if (mCursorRow < mScrollTop || mCursorRow > mScrollBottom) return;
        for (int i = 0; i < count; ++i)
        {
            for (int r = mScrollBottom; r > mCursorRow; --r) mScreen[r] = mScreen[r - 1];
            mScreen[mCursorRow] = MakeBlankRow();
        }
        for (int r = mCursorRow; r <= mScrollBottom; ++r) MarkDirty(r);
    }

    void TerminalBuffer::DeleteLines(int count)
    {
        if (mCursorRow < mScrollTop || mCursorRow > mScrollBottom) return;
        for (int i = 0; i < count; ++i)
        {
            for (int r = mCursorRow; r < mScrollBottom; ++r) mScreen[r] = mScreen[r + 1];
            mScreen[mScrollBottom] = MakeBlankRow();
        }
        for (int r = mCursorRow; r <= mScrollBottom; ++r) MarkDirty(r);
    }

    void TerminalBuffer::SetScrollRegion(int top, int bottom)
    {
        top = std::clamp(top, 0, mRows - 1);
        bottom = std::clamp(bottom, 0, mRows - 1);
        if (top >= bottom) return; // xterm 과 같이 잘못된 영역은 무시
        mScrollTop = top;
        mScrollBottom = bottom;
        MoveCursorToOrigin(0, 0); // DECSTBM 은 커서를 (origin 기준) 홈으로 보낸다
    }

    void TerminalBuffer::ScrollUp(int count)
    {
        for (int i = 0; i < count; ++i) NewLineAt(mScrollTop);
    }

    void TerminalBuffer::ScrollDown(int count)
    {
        for (int i = 0; i < count; ++i)
        {
            for (int r = mScrollBottom; r > mScrollTop; --r) mScreen[r] = mScreen[r - 1];
            mScreen[mScrollTop] = MakeBlankRow();
        }
        for (int r = mScrollTop; r <= mScrollBottom; ++r) MarkDirty(r);
    }
}
