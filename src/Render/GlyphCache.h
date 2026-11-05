#pragma once
#include <dwrite.h>
#include <wrl/client.h>
#include <unordered_map>

namespace Cutty
{
    // 코드포인트 -> 글리프 인덱스 변환 결과를 캐싱해 매 프레임 GetGlyphIndices 호출을 줄인다.
    // 폰트가 바뀌면 Reset() 으로 캐시를 비워야 한다.
    class GlyphCache
    {
    public:
        void Reset(Microsoft::WRL::ComPtr<IDWriteFontFace> fontFace)
        {
            mFontFace = std::move(fontFace);
            mCache.clear();
        }

        UINT16 GetGlyphIndex(char32_t codePoint)
        {
            auto it = mCache.find(codePoint);
            if (it != mCache.end()) return it->second;

            UINT32 cp = static_cast<UINT32>(codePoint);
            UINT16 glyph = 0;
            if (mFontFace) mFontFace->GetGlyphIndicesW(&cp, 1, &glyph);
            mCache.emplace(codePoint, glyph);
            return glyph;
        }

        IDWriteFontFace* FontFace() const { return mFontFace.Get(); }

    private:
        Microsoft::WRL::ComPtr<IDWriteFontFace> mFontFace;
        std::unordered_map<char32_t, UINT16> mCache;
    };
}
