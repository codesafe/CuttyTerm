#include "OptionsDialog.h"
#include "../Resources/resource.h"
#include <CommDlg.h>
#include <string>

#pragma comment(lib, "comdlg32.lib")

namespace Cutty
{
    namespace
    {
        struct DialogContext
        {
            AppConfig original;  // 취소 시 되돌릴 값
            AppConfig preview;   // 현재 미리보기 중인 값 (OK 시 이 값이 최종 반영됨)
            const std::function<void(const AppConfig&)>* onPreview = nullptr;
        };

        int CALLBACK EnumFixedPitchFontsProc(const LOGFONTW* lf, const TEXTMETRICW* tm, DWORD, LPARAM lParam)
        {
            if (tm->tmPitchAndFamily & TMPF_FIXED_PITCH) return 1; // 이 비트가 서있으면 가변폭 -> 건너뜀
            HWND combo = reinterpret_cast<HWND>(lParam);
            if (SendMessageW(combo, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(lf->lfFaceName)) == CB_ERR)
            {
                SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(lf->lfFaceName));
            }
            return 1;
        }

        void ApplyPreview(DialogContext* ctx)
        {
            if (ctx->onPreview) (*ctx->onPreview)(ctx->preview);
        }

        void PickColor(HWND dlg, uint32_t& target)
        {
            static COLORREF customColors[16] = { 0 };
            CHOOSECOLORW cc{};
            cc.lStructSize = sizeof(cc);
            cc.hwndOwner = dlg;
            // Cell 은 0x00RRGGBB, COLORREF 는 0x00BBGGRR 이므로 바이트 순서를 바꿔줘야 한다.
            uint32_t rgb = target;
            cc.rgbResult = RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
            cc.lpCustColors = customColors;
            cc.Flags = CC_FULLOPEN | CC_RGBINIT;
            if (ChooseColorW(&cc))
            {
                target = (static_cast<uint32_t>(GetRValue(cc.rgbResult)) << 16) |
                         (static_cast<uint32_t>(GetGValue(cc.rgbResult)) << 8) |
                         GetBValue(cc.rgbResult);
            }
        }

        INT_PTR CALLBACK OptionsDialogProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam)
        {
            DialogContext* ctx = reinterpret_cast<DialogContext*>(GetWindowLongPtrW(dlg, GWLP_USERDATA));

            switch (msg)
            {
            case WM_INITDIALOG:
            {
                ctx = reinterpret_cast<DialogContext*>(lParam);
                SetWindowLongPtrW(dlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ctx));

                HWND combo = GetDlgItem(dlg, IDC_OPT_FONTNAME);
                HDC dc = GetDC(dlg);
                LOGFONTW lf{};
                lf.lfCharSet = DEFAULT_CHARSET;
                EnumFontFamiliesExW(dc, &lf, EnumFixedPitchFontsProc, reinterpret_cast<LPARAM>(combo), 0);
                ReleaseDC(dlg, dc);

                SendMessageW(combo, CB_SELECTSTRING, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(ctx->preview.fontFamily.c_str()));
                SetDlgItemInt(dlg, IDC_OPT_FONTSIZE, static_cast<UINT>(ctx->preview.fontSize), FALSE);
                return TRUE;
            }

            case WM_COMMAND:
            {
                if (!ctx) break;
                WORD id = LOWORD(wParam);
                WORD code = HIWORD(wParam);

                if (id == IDC_OPT_FONTNAME && code == CBN_SELCHANGE)
                {
                    wchar_t buf[LF_FACESIZE];
                    GetDlgItemTextW(dlg, IDC_OPT_FONTNAME, buf, LF_FACESIZE);
                    ctx->preview.fontFamily = buf;
                    ApplyPreview(ctx);
                }
                else if (id == IDC_OPT_FONTSIZE && code == EN_CHANGE)
                {
                    BOOL ok = FALSE;
                    UINT size = GetDlgItemInt(dlg, IDC_OPT_FONTSIZE, &ok, FALSE);
                    if (ok && size > 0)
                    {
                        ctx->preview.fontSize = static_cast<float>(size);
                        ApplyPreview(ctx);
                    }
                }
                else if (id == IDC_OPT_FGCOLOR && code == BN_CLICKED)
                {
                    PickColor(dlg, ctx->preview.foregroundColor);
                    ApplyPreview(ctx);
                }
                else if (id == IDC_OPT_BGCOLOR && code == BN_CLICKED)
                {
                    PickColor(dlg, ctx->preview.backgroundColor);
                    ApplyPreview(ctx);
                }
                else if (id == IDOK)
                {
                    EndDialog(dlg, IDOK);
                }
                else if (id == IDCANCEL)
                {
                    EndDialog(dlg, IDCANCEL);
                }
                break;
            }

            default:
                break;
            }
            return FALSE;
        }
    }

    bool OptionsDialog::Show(HWND parent, HINSTANCE instance, AppConfig& config,
                              const std::function<void(const AppConfig&)>& onPreview)
    {
        DialogContext ctx;
        ctx.original = config;
        ctx.preview = config;
        ctx.onPreview = &onPreview;

        INT_PTR result = DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_OPTIONS), parent,
                                          OptionsDialogProc, reinterpret_cast<LPARAM>(&ctx));

        if (result == IDOK)
        {
            config = ctx.preview;
            return true;
        }

        onPreview(ctx.original); // 취소: 미리보기 롤백
        return false;
    }
}
