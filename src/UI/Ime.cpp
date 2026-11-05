#include "Ime.h"
#include <imm.h>

#pragma comment(lib, "imm32.lib")

namespace Cutty
{
    bool ImeComposition::OnComposition(HWND hwnd, LPARAM lParam)
    {
        if (!(lParam & GCS_COMPSTR)) return false;

        HIMC himc = ImmGetContext(hwnd);
        if (!himc) return false;

        LONG byteLen = ImmGetCompositionStringW(himc, GCS_COMPSTR, nullptr, 0);
        if (byteLen > 0)
        {
            mText.resize(byteLen / sizeof(wchar_t));
            ImmGetCompositionStringW(himc, GCS_COMPSTR, mText.data(), byteLen);
        }
        else
        {
            mText.clear();
        }

        ImmReleaseContext(hwnd, himc);
        return true;
    }

    void ImeComposition::SetCompositionPosition(HWND hwnd, int x, int y)
    {
        HIMC himc = ImmGetContext(hwnd);
        if (!himc) return;

        COMPOSITIONFORM cf{};
        cf.dwStyle = CFS_POINT;
        cf.ptCurrentPos.x = x;
        cf.ptCurrentPos.y = y;
        ImmSetCompositionWindow(himc, &cf);

        CANDIDATEFORM caf{};
        caf.dwIndex = 0;
        caf.dwStyle = CFS_CANDIDATEPOS;
        caf.ptCurrentPos.x = x;
        caf.ptCurrentPos.y = y;
        ImmSetCandidateWindow(himc, &caf);

        ImmReleaseContext(hwnd, himc);
    }
}
