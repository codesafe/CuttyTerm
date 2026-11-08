#pragma once
#include "D2DContext.h"
#include "GlyphCache.h"
#include "../Terminal/TerminalBuffer.h"
#include <string>
#include <optional>
#include <unordered_map>

namespace Cutty
{
    // IME 조합 중인 문자열을 커서 위치에 겹쳐 그리기 위한 정보.
    struct ImeOverlay
    {
        std::wstring text;
        int cursorOffsetInChars = 0; // 조합 문자열 내에서 커서가 있는 위치(표시용, 미사용시 0)
    };

    // 마우스로 드래그한 선택 영역 (정규화됨: start <= end, end 칸 포함, 컬럼은 가로 스크롤 무관한 절대값).
    struct TextSelection
    {
        int64_t startLine = 0;
        int startColumn = 0;
        int64_t endLine = 0;
        int endColumn = 0;
    };

    // TerminalBuffer 의 셀 그리드를 Direct2D + DirectWrite 로 렌더링한다.
    // 셀 크기는 고정폭 폰트의 메트릭에서 계산하며, 변경된 행(dirty)만 다시 그리도록
    // MainWindow 쪽에서 무효화 범위를 조절한다 (여기서는 항상 buffer 전체를 그린다).
    class TextRenderer
    {
    public:
        bool Initialize(ID2D1DeviceContext* d2dContext, IDWriteFactory* dwriteFactory);
        bool SetFont(const std::wstring& familyName, float pointSize);

        float CellWidth() const { return mCellWidth; }
        float CellHeight() const { return mCellHeight; }

        void Render(const TerminalBuffer& buffer, int scrollOffset, int horizontalOffset, uint32_t defaultBackground,
                    bool cursorVisible, bool windowFocused, const ImeOverlay* imeOverlay,
                    const TextSelection* selection);

    private:
        // hOffset: 가로 스크롤로 왼쪽에서 몇 칸을 건너뛰고 그릴지. viewportCols: 실제로 그릴 칸 수.
        void DrawRow(const std::vector<Cell>& row, float y, int hOffset, int viewportCols);
        static bool IsBlockElement(char32_t cp);
        void DrawBlockElement(char32_t cp, float x, float y, uint32_t fg);
        void DrawCursor(int column, float y, bool filled);
        void DrawSelection(const TextSelection& selection, int64_t lineId, int lineLength, float y, int hOffset);
        void DrawImeOverlay(const ImeOverlay& overlay, int cursorRow, int cursorColumn);

        Microsoft::WRL::ComPtr<IDWriteFontFace> CreateFontFace(const std::wstring& familyName);
        // codePoint 를 그릴 때 사용할 GlyphCache(폰트) 를 돌려준다. 기본 폰트에 없을 법한
        // 한글/일본어 등은 CharWidth.h 의 휴리스틱에 따라 대체 폰트를 지연 생성/캐싱한다.
        GlyphCache& CacheForCodepoint(char32_t codePoint);

        ID2D1DeviceContext* mD2DContext = nullptr; // non-owning
        IDWriteFactory* mDWriteFactory = nullptr;   // non-owning
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> mBrush;
        GlyphCache mGlyphCache;                                    // 기본(설정된) 폰트
        std::unordered_map<std::wstring, GlyphCache> mFallbackCaches; // 폰트 패밀리명 -> 대체 폰트 캐시

        std::wstring mFontFamily = L"Consolas";
        float mPointSize = 14.0f;
        float mEmSizeDip = 18.66f;
        float mCellWidth = 9.0f;
        float mCellHeight = 20.0f;
        float mBaseline = 16.0f;

        int mLastCursorRow = 0;
        int mLastCursorColumn = 0;
    };
}
