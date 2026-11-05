#pragma once
#include <Windows.h>
#include <string>

namespace Cutty
{
    // IME 조합 문자열을 추적하고, 조합/후보 창 위치를 커서 근처로 지정하는 헬퍼.
    class ImeComposition
    {
    public:
        // WM_IME_COMPOSITION 처리. GCS_COMPSTR 비트가 없으면 false.
        bool OnComposition(HWND hwnd, LPARAM lParam);
        void Clear() { mText.clear(); }
        const std::wstring& Text() const { return mText; }
        bool IsComposing() const { return !mText.empty(); }

        static void SetCompositionPosition(HWND hwnd, int x, int y);

    private:
        std::wstring mText;
    };
}
