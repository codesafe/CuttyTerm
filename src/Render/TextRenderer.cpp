#include "TextRenderer.h"
#include "../Terminal/CharWidth.h"
#include <vector>
#include <cmath>

using Microsoft::WRL::ComPtr;

namespace Cutty
{
    bool TextRenderer::Initialize(ID2D1DeviceContext* d2dContext, IDWriteFactory* dwriteFactory)
    {
        mD2DContext = d2dContext;
        mDWriteFactory = dwriteFactory;
        HRESULT hr = mD2DContext->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), mBrush.GetAddressOf());
        if (FAILED(hr)) return false;
        return SetFont(mFontFamily, mPointSize);
    }

    ComPtr<IDWriteFontFace> TextRenderer::CreateFontFace(const std::wstring& familyName)
    {
        ComPtr<IDWriteFontCollection> collection;
        mDWriteFactory->GetSystemFontCollection(collection.GetAddressOf(), FALSE);

        UINT32 index = 0;
        BOOL found = FALSE;
        collection->FindFamilyName(familyName.c_str(), &index, &found);
        if (!found) index = 0; // 못 찾으면 첫 폰트로 폴백

        ComPtr<IDWriteFontFamily> family;
        collection->GetFontFamily(index, family.GetAddressOf());

        ComPtr<IDWriteFont> font;
        family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, font.GetAddressOf());

        ComPtr<IDWriteFontFace> fontFace;
        font->CreateFontFace(fontFace.GetAddressOf());
        return fontFace;
    }

    GlyphCache& TextRenderer::CacheForCodepoint(char32_t codePoint)
    {
        const wchar_t* fallbackFamily = GetFallbackFontFamily(codePoint);
        if (!fallbackFamily) return mGlyphCache;

        // 기본 폰트가 이미 이 글리프를 갖고 있다면 굳이 대체 폰트로 바꾸지 않는다
        // (예: 영문판 Consolas 로도 표시 가능한 기호 등).
        if (mGlyphCache.GetGlyphIndex(codePoint) != 0) return mGlyphCache;

        auto it = mFallbackCaches.find(fallbackFamily);
        if (it == mFallbackCaches.end())
        {
            GlyphCache cache;
            cache.Reset(CreateFontFace(fallbackFamily));
            it = mFallbackCaches.emplace(fallbackFamily, std::move(cache)).first;
        }
        return it->second;
    }

    bool TextRenderer::SetFont(const std::wstring& familyName, float pointSize)
    {
        ComPtr<IDWriteFontFace> fontFace = CreateFontFace(familyName);
        mFallbackCaches.clear();

        mFontFamily = familyName;
        mPointSize = pointSize;
        mEmSizeDip = pointSize * 96.0f / 72.0f;

        DWRITE_FONT_METRICS metrics;
        fontFace->GetMetrics(&metrics);
        float unitsToDip = mEmSizeDip / static_cast<float>(metrics.designUnitsPerEm);
        mCellHeight = std::ceil((metrics.ascent + metrics.descent + metrics.lineGap) * unitsToDip);
        mBaseline = std::ceil(metrics.ascent * unitsToDip);

        UINT32 sampleChar = L'M';
        UINT16 sampleGlyph = 0;
        fontFace->GetGlyphIndicesW(&sampleChar, 1, &sampleGlyph);
        DWRITE_GLYPH_METRICS glyphMetrics{};
        fontFace->GetDesignGlyphMetrics(&sampleGlyph, 1, &glyphMetrics, FALSE);
        mCellWidth = std::ceil(glyphMetrics.advanceWidth * unitsToDip);
        if (mCellWidth <= 0) mCellWidth = mEmSizeDip * 0.6f;

        mGlyphCache.Reset(fontFace);
        return true;
    }

    namespace
    {
        // 셀의 실제 글자색: 반전(reverse)과 흐리게(dim, 배경색과 반반 섞음)를 반영한다.
        uint32_t EffectiveForeground(const Cell& cell)
        {
            uint32_t fg = cell.reverse ? cell.background : cell.foreground;
            if (!cell.dim) return fg;
            uint32_t bg = cell.reverse ? cell.foreground : cell.background;
            auto mix = [&](int shift) { return (((fg >> shift) & 0xFF) + ((bg >> shift) & 0xFF)) / 2 << shift; };
            return mix(16) | mix(8) | mix(0);
        }
    }

    // Block Elements (U+2580-U+259F). 폰트 글리프는 셀 크기와 어긋나 틈이 생기거나
    // 깨져 보이므로(예: Claude Code 마스코트) 셀 전체에 맞춰 사각형으로 직접 그린다.
    bool TextRenderer::IsBlockElement(char32_t cp)
    {
        return cp >= 0x2580 && cp <= 0x259F;
    }

    void TextRenderer::DrawBlockElement(char32_t cp, float x, float y, uint32_t fg)
    {
        const float w = mCellWidth, h = mCellHeight;
        const float x2 = x + w, y2 = y + h, mx = x + w / 2, my = y + h / 2;
        auto fill = [&](float l, float t, float r, float b, float alpha = 1.0f)
        {
            mBrush->SetColor(D2D1::ColorF(fg, alpha));
            mD2DContext->FillRectangle(D2D1::RectF(l, t, r, b), mBrush.Get());
        };

        if (cp == 0x2580) fill(x, y, x2, my);                                           // ▀
        else if (cp >= 0x2581 && cp <= 0x2587) fill(x, y2 - h * (cp - 0x2580) / 8.0f, x2, y2); // ▁..▇
        else if (cp == 0x2588) fill(x, y, x2, y2);                                      // █
        else if (cp >= 0x2589 && cp <= 0x258F) fill(x, y, x + w * (0x2590 - cp) / 8.0f, y2);   // ▉..▏
        else if (cp == 0x2590) fill(mx, y, x2, y2);                                     // ▐
        else if (cp == 0x2591) fill(x, y, x2, y2, 0.25f);                               // ░
        else if (cp == 0x2592) fill(x, y, x2, y2, 0.5f);                                // ▒
        else if (cp == 0x2593) fill(x, y, x2, y2, 0.75f);                               // ▓
        else if (cp == 0x2594) fill(x, y, x2, y + h / 8.0f);                            // ▔
        else if (cp == 0x2595) fill(x2 - w / 8.0f, y, x2, y2);                          // ▕
        else
        {
            // 사분 블록 ▖▗▘▙▚▛▜▝▞▟ : bit0=좌하, bit1=우하, bit2=좌상, bit3=우상
            static const uint8_t kQuadrants[10] = { 0x01, 0x02, 0x04, 0x07,0x06, 0x0D, 0x0E, 0x08, 0x09, 0x0B };
            uint8_t bits = kQuadrants[cp - 0x2596];
            if (bits & 0x01) fill(x, my, mx, y2);
            if (bits & 0x02) fill(mx, my, x2, y2);
            if (bits & 0x04) fill(x, y, mx, my);
            if (bits & 0x08) fill(mx, y, x2, my);
        }
    }

    void TextRenderer::DrawRow(const std::vector<Cell>& row, float y, int hOffset, int viewportCols)
    {
        // 실제 그릴 절대 컬럼 범위 [hOffset, columns). 스크롤백 등 저장 폭이 좁은
        // 행은 그만큼만 그리고 나머지는 배경색으로 남겨둔다(범위 밖 접근 방지).
        const int columns = std::min(static_cast<int>(row.size()), hOffset + viewportCols);
        auto screenX = [&](int absoluteCol) { return static_cast<float>(absoluteCol - hOffset) * mCellWidth; };

        // ---- 1단계: 배경색 런(run) 채우기 ----
        int col = hOffset;
        while (col < columns)
        {
            uint32_t bg = row[col].reverse ? row[col].foreground : row[col].background;
            int start = col;
            while (col < columns)
            {
                uint32_t curBg = row[col].reverse ? row[col].foreground : row[col].background;
                if (curBg != bg) break;
                ++col;
            }
            D2D1_RECT_F rect = D2D1::RectF(screenX(start), y, screenX(col), y + mCellHeight);
            mBrush->SetColor(D2D1::ColorF(bg, 1.0f));
            mD2DContext->FillRectangle(rect, mBrush.Get());
        }

        // ---- 2단계: 텍스트 런 그리기 (전경색/굵게 단위로 묶음) ----
        std::vector<UINT16> glyphIndices;
        std::vector<FLOAT> advances;
        std::vector<DWRITE_GLYPH_OFFSET> offsets;
        glyphIndices.reserve(columns - hOffset);
        advances.reserve(columns - hOffset);
        offsets.reserve(columns - hOffset);

        col = hOffset;
        while (col < columns)
        {
            if (row[col].isTrailing || row[col].hidden) { ++col; continue; }

            if (IsBlockElement(row[col].codepoint))
            {
                DrawBlockElement(row[col].codepoint, screenX(col), y, EffectiveForeground(row[col]));
                ++col;
                continue;
            }

            const Cell& first = row[col];
            uint32_t fg = EffectiveForeground(first);
            bool bold = first.bold;
            bool italic = first.italic;
            GlyphCache* fontCache = &CacheForCodepoint(first.codepoint);
            int runStartCol = col;

            glyphIndices.clear();
            advances.clear();
            offsets.clear();

            while (col < columns)
            {
                const Cell& cell = row[col];
                if (cell.isTrailing) { ++col; continue; }
                if (cell.hidden || IsBlockElement(cell.codepoint)) break;
                uint32_t curFg = EffectiveForeground(cell);
                GlyphCache* curFontCache = &CacheForCodepoint(cell.codepoint);
                if (curFg != fg || cell.bold != bold || cell.italic != italic || curFontCache != fontCache) break;

                float advance = mCellWidth * (cell.isWide ? 2.0f : 1.0f);
                glyphIndices.push_back(fontCache->GetGlyphIndex(cell.codepoint));
                advances.push_back(advance);
                offsets.push_back(DWRITE_GLYPH_OFFSET{ 0, 0 });
                ++col;
            }

            if (!glyphIndices.empty())
            {
                DWRITE_GLYPH_RUN run{};
                run.fontFace = fontCache->FontFace();
                run.fontEmSize = mEmSizeDip;
                run.glyphCount = static_cast<UINT32>(glyphIndices.size());
                run.glyphIndices = glyphIndices.data();
                run.glyphAdvances = advances.data();
                run.glyphOffsets = offsets.data();

                mBrush->SetColor(D2D1::ColorF(fg, 1.0f));
                D2D1_POINT_2F origin = D2D1::Point2F(screenX(runStartCol), y + mBaseline);

                // 기울임: 기울임꼴 폰트 없이 기준선을 축으로 살짝 기울여 그린다.
                D2D1_MATRIX_3X2_F oldTransform;
                if (italic)
                {
                    mD2DContext->GetTransform(&oldTransform);
                    mD2DContext->SetTransform(D2D1::Matrix3x2F::Skew(-12.0f, 0.0f, origin) * oldTransform);
                }

                mD2DContext->DrawGlyphRun(origin, &run, mBrush.Get());
                if (bold) // 볼드 전용 폰트가 없을 때의 간단한 페이크 볼드(살짝 오른쪽으로 겹쳐 그림)
                {
                    D2D1_POINT_2F origin2 = D2D1::Point2F(origin.x + 0.4f, origin.y);
                    mD2DContext->DrawGlyphRun(origin2, &run, mBrush.Get());
                }

                if (italic) mD2DContext->SetTransform(oldTransform);
            }
        }

        // ---- 3단계: 밑줄 / 취소선 ----
        auto drawLines = [&](bool Cell::* flag, float top, float bottom)
        {
            int c = hOffset;
            while (c < columns)
            {
                if (!(row[c].*flag) || row[c].hidden) { ++c; continue; }
                uint32_t fg = EffectiveForeground(row[c]);
                int start = c;
                while (c < columns && (row[c].*flag) && !row[c].hidden && EffectiveForeground(row[c]) == fg) ++c;
                D2D1_RECT_F rect = D2D1::RectF(screenX(start), y + top, screenX(c), y + bottom);
                mBrush->SetColor(D2D1::ColorF(fg, 1.0f));
                mD2DContext->FillRectangle(rect, mBrush.Get());
            }
        };
        drawLines(&Cell::underline, mCellHeight - 2, mCellHeight - 1);
        float strikeY = std::floor(mBaseline * 0.65f);
        drawLines(&Cell::strikethrough, strikeY, strikeY + 1);
    }

    void TextRenderer::DrawCursor(int column, float y, bool filled)
    {
        D2D1_RECT_F rect = D2D1::RectF(column * mCellWidth, y, (column + 1) * mCellWidth, y + mCellHeight);
        mBrush->SetColor(D2D1::ColorF(D2D1::ColorF::LightGray));
        if (filled) mD2DContext->FillRectangle(rect, mBrush.Get());
        else mD2DContext->DrawRectangle(rect, mBrush.Get(), 1.0f);
    }

    void TextRenderer::DrawSelection(const TextSelection& selection, int64_t lineId, int lineLength, float y, int hOffset)
    {
        if (lineId < selection.startLine || lineId > selection.endLine) return;
        int first = (lineId == selection.startLine) ? selection.startColumn : 0;
        int last = (lineId == selection.endLine) ? selection.endColumn : lineLength - 1;
        if (last < first) return;

        // 반투명 파란색을 글자 위에 덮어 선택 영역을 표시한다 (글자색과 무관하게 잘 보임).
        D2D1_RECT_F rect = D2D1::RectF((first - hOffset) * mCellWidth, y, (last + 1 - hOffset) * mCellWidth, y + mCellHeight);
        mBrush->SetColor(D2D1::ColorF(0x3A78C8, 0.45f));
        mD2DContext->FillRectangle(rect, mBrush.Get());
    }

    void TextRenderer::DrawImeOverlay(const ImeOverlay& overlay, int cursorRow, int cursorColumn)
    {
        if (overlay.text.empty()) return;

        float y = cursorRow * mCellHeight;
        float x = cursorColumn * mCellWidth;
        float width = mCellWidth * static_cast<float>(overlay.text.size());

        D2D1_RECT_F bgRect = D2D1::RectF(x, y, x + width, y + mCellHeight);
        mBrush->SetColor(D2D1::ColorF(0x264F78, 1.0f)); // 조합 중 배경(파란빛 강조)
        mD2DContext->FillRectangle(bgRect, mBrush.Get());

        mBrush->SetColor(D2D1::ColorF(D2D1::ColorF::White));

        // 조합 문자열은 폰트가 섞일 수 있으므로(예: 한글 조합 중 영문) 문자마다
        // 알맞은 폰트를 찾아 같은 폰트가 이어지는 구간별로 나눠 그린다.
        std::vector<UINT16> glyphIndices;
        std::vector<FLOAT> advances;
        size_t i = 0;
        float penX = x;
        while (i < overlay.text.size())
        {
            char32_t cp = overlay.text[i];
            GlyphCache& fontCache = CacheForCodepoint(cp);
            glyphIndices.clear();
            advances.clear();
            float runStartX = penX;

            while (i < overlay.text.size() && &CacheForCodepoint(overlay.text[i]) == &fontCache)
            {
                glyphIndices.push_back(fontCache.GetGlyphIndex(overlay.text[i]));
                advances.push_back(mCellWidth);
                penX += mCellWidth;
                ++i;
            }

            DWRITE_GLYPH_RUN run{};
            run.fontFace = fontCache.FontFace();
            run.fontEmSize = mEmSizeDip;
            run.glyphCount = static_cast<UINT32>(glyphIndices.size());
            run.glyphIndices = glyphIndices.data();
            run.glyphAdvances = advances.data();
            mD2DContext->DrawGlyphRun(D2D1::Point2F(runStartX, y + mBaseline), &run, mBrush.Get());
        }

        D2D1_RECT_F underline = D2D1::RectF(x, y + mCellHeight - 2, x + width, y + mCellHeight - 1);
        mD2DContext->FillRectangle(underline, mBrush.Get());
    }

    void TextRenderer::Render(const TerminalBuffer& buffer, int scrollOffset, int horizontalOffset,
                               uint32_t defaultBackground, bool cursorVisible, bool windowFocused,
                               const ImeOverlay* imeOverlay, const TextSelection* selection)
    {
        mD2DContext->Clear(D2D1::ColorF(defaultBackground, 1.0f));

        const int viewportCols = buffer.Columns();
        for (int row = 0; row < buffer.Rows(); ++row)
        {
            const std::vector<Cell>& line = buffer.ViewLine(row, scrollOffset);
            DrawRow(line, row * mCellHeight, horizontalOffset, viewportCols);
            if (selection)
            {
                DrawSelection(*selection, buffer.ViewLineId(row, scrollOffset), static_cast<int>(line.size()),
                              row * mCellHeight, horizontalOffset);
            }
        }

        // 커서/IME 오버레이는 세로/가로 스크롤이 모두 맨 끝(0)일 때만 실제 커서 위치와
        // 화면 위치가 일치하므로, 스크롤된 상태에서는 그리지 않는다.
        if (scrollOffset == 0 && horizontalOffset == 0 && cursorVisible)
        {
            mLastCursorRow = buffer.CursorRow();
            mLastCursorColumn = buffer.CursorColumn();
            DrawCursor(mLastCursorColumn, mLastCursorRow * mCellHeight, windowFocused);
        }

        if (imeOverlay && scrollOffset == 0 && horizontalOffset == 0)
        {
            DrawImeOverlay(*imeOverlay, mLastCursorRow, mLastCursorColumn);
        }
    }
}
