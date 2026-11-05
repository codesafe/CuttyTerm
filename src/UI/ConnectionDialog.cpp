#include "ConnectionDialog.h"
#include "../Resources/resource.h"
#include <commdlg.h>
#include <string>

#pragma comment(lib, "comdlg32.lib")

namespace Cutty
{
    namespace
    {
        // 인코딩 콤보박스 항목 (0 = 자동 감지)
        struct EncodingOption
        {
            unsigned codePage;
            const wchar_t* label;
        };
        constexpr EncodingOption kEncodings[] =
        {
            { 0, L"자동 감지" },
            { 65001, L"UTF-8" },
            { 949, L"CP949 (EUC-KR)" },
            { 932, L"Shift-JIS" },
            { 51932, L"EUC-JP" },
            { 936, L"GBK (중국어 간체)" },
            { 950, L"Big5 (중국어 번체)" },
            { 1252, L"Latin-1 (서유럽)" },
            { 437, L"CP437 (DOS)" },
        };

        int EncodingIndex(unsigned codePage)
        {
            for (int i = 0; i < static_cast<int>(std::size(kEncodings)); ++i)
            {
                if (kEncodings[i].codePage == codePage) return i;
            }
            return 0;
        }

        struct DialogContext
        {
            AppConfig* config = nullptr;
            std::optional<ConnectionProfile> result;
        };

        void RefreshList(HWND dlg, const AppConfig& config)
        {
            HWND list = GetDlgItem(dlg, IDC_CONN_LIST);
            SendMessageW(list, LB_RESETCONTENT, 0, 0);
            for (const auto& profile : config.connections)
            {
                SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(profile.name.c_str()));
            }
        }

        void FillFieldsFromProfile(HWND dlg, const ConnectionProfile& profile)
        {
            SetDlgItemTextW(dlg, IDC_CONN_NAME, profile.name.c_str());
            SetDlgItemTextW(dlg, IDC_CONN_HOST, profile.host.c_str());
            SetDlgItemInt(dlg, IDC_CONN_PORT, profile.port, FALSE);
            SetDlgItemTextW(dlg, IDC_CONN_USERNAME, profile.username.c_str());
            SetDlgItemTextW(dlg, IDC_CONN_PASSWORD, profile.password.c_str());
            SetDlgItemTextW(dlg, IDC_CONN_KEYFILE, profile.privateKeyFile.c_str());
            SendDlgItemMessageW(dlg, IDC_CONN_PROTOCOL, CB_SETCURSEL,
                                profile.protocol == ProtocolType::Ssh ? 1 : 0, 0);
            SendDlgItemMessageW(dlg, IDC_CONN_ENCODING, CB_SETCURSEL, EncodingIndex(profile.codePage), 0);
        }

        void BrowsePrivateKey(HWND dlg)
        {
            wchar_t path[MAX_PATH] = L"";
            GetDlgItemTextW(dlg, IDC_CONN_KEYFILE, path, MAX_PATH);
            OPENFILENAMEW ofn{};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = dlg;
            ofn.lpstrFilter = L"개인키 파일 (id_*, *.pem, *.key)\0id_*;*.pem;*.key\0모든 파일 (*.*)\0*.*\0";
            ofn.lpstrFile = path;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrTitle = L"SSH 개인키 선택 (OpenSSH/PEM 형식)";
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
            if (GetOpenFileNameW(&ofn)) SetDlgItemTextW(dlg, IDC_CONN_KEYFILE, path);
        }

        ConnectionProfile ReadFieldsAsProfile(HWND dlg)
        {
            ConnectionProfile profile;
            wchar_t buf[256];

            GetDlgItemTextW(dlg, IDC_CONN_NAME, buf, 256); profile.name = buf;
            GetDlgItemTextW(dlg, IDC_CONN_HOST, buf, 256); profile.host = buf;
            GetDlgItemTextW(dlg, IDC_CONN_USERNAME, buf, 256); profile.username = buf;
            GetDlgItemTextW(dlg, IDC_CONN_PASSWORD, buf, 256); profile.password = buf;
            wchar_t keyPath[MAX_PATH] = L"";
            GetDlgItemTextW(dlg, IDC_CONN_KEYFILE, keyPath, MAX_PATH); profile.privateKeyFile = keyPath;

            int encodingSel = static_cast<int>(SendDlgItemMessageW(dlg, IDC_CONN_ENCODING, CB_GETCURSEL, 0, 0));
            if (encodingSel >= 0 && encodingSel < static_cast<int>(std::size(kEncodings)))
            {
                profile.codePage = kEncodings[encodingSel].codePage;
            }

            BOOL ok = FALSE;
            UINT port = GetDlgItemInt(dlg, IDC_CONN_PORT, &ok, FALSE);
            profile.port = ok ? static_cast<uint16_t>(port) : 23;

            int protoSel = static_cast<int>(SendDlgItemMessageW(dlg, IDC_CONN_PROTOCOL, CB_GETCURSEL, 0, 0));
            profile.protocol = protoSel == 1 ? ProtocolType::Ssh : ProtocolType::Telnet;
            return profile;
        }
    }

    static INT_PTR CALLBACK ConnectionDialogProcImpl(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        DialogContext* ctx = reinterpret_cast<DialogContext*>(GetWindowLongPtrW(dlg, GWLP_USERDATA));

        switch (msg)
        {
        case WM_INITDIALOG:
        {
            ctx = reinterpret_cast<DialogContext*>(lParam);
            SetWindowLongPtrW(dlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ctx));

            HWND protoCombo = GetDlgItem(dlg, IDC_CONN_PROTOCOL);
            SendMessageW(protoCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Telnet"));
            SendMessageW(protoCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"SSH"));
            SendMessageW(protoCombo, CB_SETCURSEL, 0, 0);

            HWND encodingCombo = GetDlgItem(dlg, IDC_CONN_ENCODING);
            for (const auto& option : kEncodings)
            {
                SendMessageW(encodingCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(option.label));
            }
            SendMessageW(encodingCombo, CB_SETCURSEL, 0, 0);

            RefreshList(dlg, *ctx->config);
            return TRUE;
        }

        case WM_COMMAND:
        {
            if (!ctx) break;
            WORD id = LOWORD(wParam);
            WORD code = HIWORD(wParam);

            if (id == IDC_CONN_LIST && code == LBN_SELCHANGE)
            {
                int sel = static_cast<int>(SendDlgItemMessageW(dlg, IDC_CONN_LIST, LB_GETCURSEL, 0, 0));
                if (sel >= 0 && sel < static_cast<int>(ctx->config->connections.size()))
                {
                    FillFieldsFromProfile(dlg, ctx->config->connections[sel]);
                }
            }
            else if (id == IDC_CONN_KEYBROWSE)
            {
                BrowsePrivateKey(dlg);
            }
            else if (id == IDC_CONN_ADD)
            {
                ConnectionProfile profile = ReadFieldsAsProfile(dlg);
                if (!profile.host.empty())
                {
                    if (profile.name.empty()) profile.name = profile.host;
                    ctx->config->connections.push_back(profile);
                    RefreshList(dlg, *ctx->config);
                }
            }
            else if (id == IDC_CONN_APPLY)
            {
                // 목록에서 선택한 연결을 입력란의 내용으로 수정
                int sel = static_cast<int>(SendDlgItemMessageW(dlg, IDC_CONN_LIST, LB_GETCURSEL, 0, 0));
                if (sel < 0 || sel >= static_cast<int>(ctx->config->connections.size()))
                {
                    MessageBoxW(dlg, L"수정할 연결을 목록에서 먼저 선택하세요.", L"연결 관리", MB_OK | MB_ICONINFORMATION);
                    break;
                }
                ConnectionProfile profile = ReadFieldsAsProfile(dlg);
                if (profile.host.empty())
                {
                    MessageBoxW(dlg, L"호스트를 입력하세요.", L"연결 관리", MB_OK | MB_ICONWARNING);
                    break;
                }
                if (profile.name.empty()) profile.name = profile.host;
                ctx->config->connections[sel] = profile;
                RefreshList(dlg, *ctx->config);
                SendDlgItemMessageW(dlg, IDC_CONN_LIST, LB_SETCURSEL, sel, 0);
                SetDlgItemTextW(dlg, IDC_CONN_NAME, profile.name.c_str());
                ctx->config->Save(); // 바로 파일에도 반영
            }
            else if (id == IDC_CONN_DELETE)
            {
                int sel = static_cast<int>(SendDlgItemMessageW(dlg, IDC_CONN_LIST, LB_GETCURSEL, 0, 0));
                if (sel >= 0 && sel < static_cast<int>(ctx->config->connections.size()))
                {
                    ctx->config->connections.erase(ctx->config->connections.begin() + sel);
                    RefreshList(dlg, *ctx->config);
                }
            }
            else if (id == IDC_CONN_CONNECT)
            {
                ConnectionProfile profile = ReadFieldsAsProfile(dlg);
                if (!profile.host.empty())
                {
                    ctx->result = profile;
                    EndDialog(dlg, IDOK);
                }
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

    std::optional<ConnectionProfile> ConnectionDialog::Show(HWND parent, HINSTANCE instance, AppConfig& config)
    {
        DialogContext ctx;
        ctx.config = &config;

        DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_CONNECTION), parent,
                         ConnectionDialogProcImpl, reinterpret_cast<LPARAM>(&ctx));
        return ctx.result;
    }

    namespace
    {
        struct LoginContext
        {
            std::optional<LoginDialog::Result> result;
            std::wstring defaultUsername;
        };
    }

    static INT_PTR CALLBACK LoginDialogProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        LoginContext* ctx = reinterpret_cast<LoginContext*>(GetWindowLongPtrW(dlg, GWLP_USERDATA));

        switch (msg)
        {
        case WM_INITDIALOG:
            ctx = reinterpret_cast<LoginContext*>(lParam);
            SetWindowLongPtrW(dlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ctx));
            SetDlgItemTextW(dlg, IDC_LOGIN_USERNAME, ctx->defaultUsername.c_str());
            return TRUE;

        case WM_COMMAND:
            if (!ctx) break;
            if (LOWORD(wParam) == IDOK)
            {
                wchar_t user[256], pass[256];
                GetDlgItemTextW(dlg, IDC_LOGIN_USERNAME, user, 256);
                GetDlgItemTextW(dlg, IDC_LOGIN_PASSWORD, pass, 256);
                ctx->result = LoginDialog::Result{ user, pass };
                EndDialog(dlg, IDOK);
            }
            else if (LOWORD(wParam) == IDCANCEL)
            {
                EndDialog(dlg, IDCANCEL);
            }
            break;

        default:
            break;
        }
        return FALSE;
    }

    std::optional<LoginDialog::Result> LoginDialog::Show(HWND parent, HINSTANCE instance, const std::wstring& defaultUsername)
    {
        LoginContext ctx;
        ctx.defaultUsername = defaultUsername;
        DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_LOGIN), parent, LoginDialogProc, reinterpret_cast<LPARAM>(&ctx));
        return ctx.result;
    }

    namespace
    {
        struct AuthPromptContext
        {
            std::wstring title;
            std::wstring message;
            bool echo = false;
            std::optional<std::wstring> result;
        };
    }

    static INT_PTR CALLBACK AuthPromptDialogProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        auto ctx = reinterpret_cast<AuthPromptContext*>(GetWindowLongPtrW(dlg, GWLP_USERDATA));

        switch (msg)
        {
        case WM_INITDIALOG:
        {
            ctx = reinterpret_cast<AuthPromptContext*>(lParam);
            SetWindowLongPtrW(dlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ctx));
            SetWindowTextW(dlg, ctx->title.c_str());
            SetDlgItemTextW(dlg, IDC_AUTH_TEXT, ctx->message.c_str());
            if (ctx->echo) SendDlgItemMessageW(dlg, IDC_AUTH_INPUT, EM_SETPASSWORDCHAR, 0, 0);
            SetForegroundWindow(dlg); // 연결 스레드에서 띄우므로 앞으로 가져온다
            SetFocus(GetDlgItem(dlg, IDC_AUTH_INPUT));
            return FALSE; // 포커스를 직접 지정함
        }

        case WM_COMMAND:
            if (!ctx) break;
            if (LOWORD(wParam) == IDOK)
            {
                wchar_t text[512];
                GetDlgItemTextW(dlg, IDC_AUTH_INPUT, text, 512);
                ctx->result = std::wstring(text);
                SecureZeroMemory(text, sizeof(text));
                EndDialog(dlg, IDOK);
            }
            else if (LOWORD(wParam) == IDCANCEL)
            {
                EndDialog(dlg, IDCANCEL);
            }
            break;

        default:
            break;
        }
        return FALSE;
    }

    std::optional<std::wstring> AuthPromptDialog::Show(HWND parent, HINSTANCE instance, const std::wstring& title,
                                                       const std::wstring& message, bool echo)
    {
        AuthPromptContext ctx{ title, message, echo, std::nullopt };
        DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_AUTHPROMPT), parent, AuthPromptDialogProc,
                        reinterpret_cast<LPARAM>(&ctx));
        return ctx.result;
    }
}
