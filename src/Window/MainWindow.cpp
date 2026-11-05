#include "MainWindow.h"
#include "../Resources/resource.h"
#include "../Net/TelnetConnection.h"
#include "../Net/SshConnection.h"
#include "../UI/OptionsDialog.h"
#include "../UI/ConnectionDialog.h"
#include "../Common/StringUtil.h"
#include <CommCtrl.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <thread>
#include <memory>
#include <objidl.h>
// NOMINMAX 환경에서 gdiplus.h 가 쓰는 min/max 를 제공한다.
namespace Gdiplus
{
    using std::min;
    using std::max;
}
#include <gdiplus.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdiplus.lib")

namespace Cutty
{
    namespace
    {
        // 툴바 아이콘은 외부 리소스 없이 GDI+ 로 런타임에 그린다. 안티에일리어싱 + 알파
        // 채널(32bpp ARGB) 로 그려 HICON 으로 만든 뒤 이미지 리스트에 넣으므로 가장자리가
        // 매끄럽다. 좌표는 kGlyphDesignSize(20) 기준이며 실제 크기에 맞춰 확대/축소된다.
        constexpr int kToolbarIconSize = 20; // 이전 13px 대비 약 50% 증가
        constexpr float kGlyphDesignSize = 20.0f;

        Gdiplus::GraphicsPath* CreateRoundRectPath(float x, float y, float w, float h, float r)
        {
            auto path = new Gdiplus::GraphicsPath();
            float d = r * 2.0f;
            path->AddArc(x, y, d, d, 180.0f, 90.0f);
            path->AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
            path->AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
            path->AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
            path->CloseFigure();
            return path;
        }

        HICON CreateToolbarIcon(void (*draw)(Gdiplus::Graphics& g))
        {
            const int size = kToolbarIconSize;
            BITMAPINFO bmi{};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = size;
            bmi.bmiHeader.biHeight = -size; // top-down
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;
            void* bits = nullptr;
            HBITMAP color = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
            if (!color) return nullptr;
            memset(bits, 0, static_cast<size_t>(size) * size * 4);

            {
                // 아이콘은 비-premultiplied 알파를 쓰므로 PixelFormat32bppARGB 로 직접 그린다.
                Gdiplus::Bitmap canvas(size, size, size * 4, PixelFormat32bppARGB, static_cast<BYTE*>(bits));
                Gdiplus::Graphics g(&canvas);
                g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
                g.ScaleTransform(size / kGlyphDesignSize, size / kGlyphDesignSize);
                draw(g);
            }

            HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr); // 알파가 있으면 마스크는 무시됨
            ICONINFO info{ TRUE, 0, 0, mask, color };
            HICON icon = CreateIconIndirect(&info);
            DeleteObject(mask);
            DeleteObject(color);
            return icon;
        }

        // 새 연결: 터미널 창(">_") + 초록색 "+" 배지
        void DrawNewConnectionGlyph(Gdiplus::Graphics& g)
        {
            std::unique_ptr<Gdiplus::GraphicsPath> window(CreateRoundRectPath(1.5f, 2.5f, 15.0f, 13.0f, 2.5f));
            Gdiplus::LinearGradientBrush body(Gdiplus::PointF(0, 2), Gdiplus::PointF(0, 16),
                                              Gdiplus::Color(255, 70, 78, 92), Gdiplus::Color(255, 34, 38, 46));
            g.FillPath(&body, window.get());
            Gdiplus::Pen border(Gdiplus::Color(255, 120, 132, 150), 1.0f);
            g.DrawPath(&border, window.get());

            Gdiplus::Pen prompt(Gdiplus::Color(255, 120, 230, 150), 1.6f);
            prompt.SetStartCap(Gdiplus::LineCapRound);
            prompt.SetEndCap(Gdiplus::LineCapRound);
            prompt.SetLineJoin(Gdiplus::LineJoinRound);
            Gdiplus::PointF chevron[3] = { { 4.5f, 6.5f }, { 7.0f, 9.0f }, { 4.5f, 11.5f } };
            g.DrawLines(&prompt, chevron, 3);
            g.DrawLine(&prompt, 8.5f, 11.5f, 11.5f, 11.5f);

            Gdiplus::SolidBrush badge(Gdiplus::Color(255, 46, 170, 90));
            g.FillEllipse(&badge, 11.0f, 11.0f, 8.5f, 8.5f);
            Gdiplus::Pen plus(Gdiplus::Color(255, 255, 255, 255), 1.5f);
            g.DrawLine(&plus, 15.25f, 13.0f, 15.25f, 17.5f);
            g.DrawLine(&plus, 13.0f, 15.25f, 17.5f, 15.25f);
        }

        // 옵션: 톱니바퀴 (톱니 8개 + 가운데 구멍)
        void DrawOptionsGlyph(Gdiplus::Graphics& g)
        {
            constexpr float cx = 10.0f, cy = 10.0f;
            Gdiplus::GraphicsPath gear;
            gear.SetFillMode(Gdiplus::FillModeWinding);
            for (int i = 0; i < 8; ++i)
            {
                Gdiplus::GraphicsPath tooth;
                tooth.AddRectangle(Gdiplus::RectF(cx - 1.6f, cy - 9.0f, 3.2f, 4.0f));
                Gdiplus::Matrix rotate;
                rotate.RotateAt(i * 45.0f, Gdiplus::PointF(cx, cy));
                tooth.Transform(&rotate);
                gear.AddPath(&tooth, FALSE);
            }
            gear.AddEllipse(cx - 6.5f, cy - 6.5f, 13.0f, 13.0f);

            Gdiplus::LinearGradientBrush metal(Gdiplus::PointF(0, 1), Gdiplus::PointF(0, 19),
                                               Gdiplus::Color(255, 255, 214, 102), Gdiplus::Color(255, 214, 140, 30));
            g.FillPath(&metal, &gear);
            Gdiplus::Pen outline(Gdiplus::Color(255, 150, 95, 20), 0.8f);
            g.DrawPath(&outline, &gear);

            // 가운데 구멍은 투명하게 뚫는다
            g.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
            Gdiplus::SolidBrush clear(Gdiplus::Color(0, 0, 0, 0));
            g.FillEllipse(&clear, cx - 2.6f, cy - 2.6f, 5.2f, 5.2f);
            g.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
            g.DrawEllipse(&outline, cx - 2.6f, cy - 2.6f, 5.2f, 5.2f);
        }

        // 탭 닫기: 빨간 원 + 흰색 X
        void DrawCloseGlyph(Gdiplus::Graphics& g)
        {
            Gdiplus::LinearGradientBrush circle(Gdiplus::PointF(0, 1.5f), Gdiplus::PointF(0, 18.5f),
                                                Gdiplus::Color(255, 240, 100, 100), Gdiplus::Color(255, 196, 43, 43));
            g.FillEllipse(&circle, 1.5f, 1.5f, 17.0f, 17.0f);
            Gdiplus::Pen cross(Gdiplus::Color(255, 255, 255, 255), 2.0f);
            cross.SetStartCap(Gdiplus::LineCapRound);
            cross.SetEndCap(Gdiplus::LineCapRound);
            g.DrawLine(&cross, 6.8f, 6.8f, 13.2f, 13.2f);
            g.DrawLine(&cross, 13.2f, 6.8f, 6.8f, 13.2f);
        }
    }

    MainWindow::MainWindow(HINSTANCE instance, AppConfig& config)
        : mInstance(instance), mConfig(config)
    {
        INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_TAB_CLASSES | ICC_BAR_CLASSES };
        InitCommonControlsEx(&icc);
        Gdiplus::GdiplusStartupInput gdiplusInput;
        Gdiplus::GdiplusStartup(&mGdiplusToken, &gdiplusInput, nullptr); // 툴바 아이콘/탭 그리기용
    }

    MainWindow::~MainWindow()
    {
        if (mTabFont) DeleteObject(mTabFont);
        Gdiplus::GdiplusShutdown(mGdiplusToken);
    }

    bool MainWindow::Create(int showCommand)
    {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = StaticWndProc;
        wc.hInstance = mInstance;
        wc.hIcon = LoadIconW(mInstance, MAKEINTRESOURCEW(IDI_APPICON));
        wc.hIconSm = wc.hIcon;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1); // 툴바 아래 틈이 생겨도 무난한 배경색
        wc.lpszClassName = L"CuttyTermMainWindow";
        RegisterClassExW(&wc);

        WNDCLASSEXW renderClass{};
        renderClass.cbSize = sizeof(renderClass);
        renderClass.style = CS_HREDRAW | CS_VREDRAW;
        renderClass.lpfnWndProc = RenderAreaStaticProc;
        renderClass.hInstance = mInstance;
        renderClass.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
        renderClass.lpszClassName = L"CuttyTermRenderArea";
        RegisterClassExW(&renderClass);

        mHwnd = CreateWindowExW(0, L"CuttyTermMainWindow", L"CuttyTerm", WS_OVERLAPPEDWINDOW,
                                 CW_USEDEFAULT, CW_USEDEFAULT, 1000, 700,
                                 nullptr, nullptr, mInstance, this);
        if (!mHwnd) return false;

        mAccelTable = LoadAcceleratorsW(mInstance, L"CuttyAccelerators");

        ShowWindow(mHwnd, showCommand);
        UpdateWindow(mHwnd);
        return true;
    }

    LRESULT CALLBACK MainWindow::StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        MainWindow* self;
        if (msg == WM_NCCREATE)
        {
            auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = reinterpret_cast<MainWindow*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->mHwnd = hwnd;
        }
        else
        {
            self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        if (self) return self->WndProc(hwnd, msg, wParam, lParam);
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    LRESULT MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_CREATE:
            OnCreate();
            return 0;

        case WM_SIZE:
            OnSize(LOWORD(lParam), HIWORD(lParam));
            return 0;

        case WM_PAINT:
        {
            // 실제 터미널 렌더링은 mRenderArea 자식 윈도우가 전담한다. 프레임 자신은
            // 툴바/탭 아래 공간이 자식 윈도우로 완전히 덮여 있으므로 업데이트 영역만 비운다.
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
            return 0;
        }

        // WM_ERASEBKGND: 프레임 자신은 hbrBackground 로 기본 처리하도록 DefWindowProc 에 위임
        // (실제 터미널 렌더링은 mRenderArea 자식 윈도우가 전담하며, 그쪽은 자체적으로
        //  WM_ERASEBKGND=1 처리해 Direct2D 프레임과의 깜빡임을 방지한다.)

        case WM_KEYDOWN:
            OnKeyDown(wParam, false);
            return 0;

        // Alt 조합과 F10 은 WM_SYSKEYDOWN/WM_SYSCHAR 로 온다. Alt+F4(종료), Alt+Space(시스템
        // 메뉴)만 Windows 에 맡기고 나머지는 터미널 입력(ESC 접두/수정키 시퀀스)으로 보낸다.
        case WM_SYSKEYDOWN:
        {
            bool alt = (lParam & (1 << 29)) != 0;
            if (wParam == VK_F4 && alt) break;
            if (OnKeyDown(wParam, alt)) return 0;
            break;
        }

        case WM_SYSCHAR:
            if (wParam == L' ') break;
            OnChar(static_cast<wchar_t>(wParam), true);
            return 0;

        case WM_SYSCOMMAND:
            // Alt 를 단독으로 눌렀다 떼면 메뉴 모드로 들어가 다음 키 입력을 빼앗기므로 막는다.
            if ((wParam & 0xFFF0) == SC_KEYMENU && lParam == 0) return 0;
            break;

        case WM_MOUSEWHEEL:
            OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
            return 0;

        case WM_CHAR:
            OnChar(static_cast<wchar_t>(wParam), false);
            return 0;

        case WM_COMMAND:
            OnCommand(wParam);
            return 0;

        case WM_NOTIFY:
            OnNotify(lParam);
            return 0;

        case WM_SETFOCUS:
        case WM_KILLFOCUS:
            InvalidateTerminalArea();
            break;

        case WM_IME_STARTCOMPOSITION:
        {
            if (TabState* tab = ActiveTab())
            {
                RECT area = TerminalAreaRect();
                int x = area.left + tab->buffer.CursorColumn() * static_cast<int>(mTextRenderer.CellWidth());
                int y = area.top + tab->buffer.CursorRow() * static_cast<int>(mTextRenderer.CellHeight());
                ImeComposition::SetCompositionPosition(hwnd, x, y);
            }
            break;
        }

        case WM_IME_COMPOSITION:
            mIme.OnComposition(hwnd, lParam);
            InvalidateTerminalArea();
            break; // GCS_RESULTSTR 확정 문자는 DefWindowProc 가 WM_CHAR 로 넘겨주므로 계속 진행

        case WM_IME_ENDCOMPOSITION:
            mIme.Clear();
            InvalidateTerminalArea();
            break;

        case kMsgConnData:
            OnConnectionData(static_cast<int>(wParam), reinterpret_cast<std::vector<uint8_t>*>(lParam));
            return 0;

        case kMsgConnClosed:
            OnConnectionClosed(static_cast<int>(wParam));
            return 0;

        case kMsgConnError:
            OnConnectionError(static_cast<int>(wParam), reinterpret_cast<std::wstring*>(lParam));
            return 0;

        case kMsgConnReady:
            OnConnectionReady(static_cast<int>(wParam), reinterpret_cast<IConnection*>(lParam));
            return 0;

        case WM_DESTROY:
            OnDestroy();
            return 0;

        default:
            break;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    void MainWindow::OnCreate()
    {
        CreateToolbar();
        CreateTabControl();
        CreateRenderArea();

        mD2DContext.Initialize(mRenderArea);
        mTextRenderer.Initialize(mD2DContext.DeviceContext(), mD2DContext.DWriteFactory());
        mTextRenderer.SetFont(mConfig.fontFamily, mConfig.fontSize);
        mLiveBackground = mConfig.backgroundColor;
        mLiveForeground = mConfig.foregroundColor;
    }

    void MainWindow::CreateRenderArea()
    {
        // 실제 터미널 화면(Direct2D)을 그리는 전용 자식 윈도우. 툴바/탭 컨트롤과
        // 형제(sibling) 관계로 두어 Win32 의 표준 자식 윈도우 겹침 규칙을 그대로 따르게 한다.
        mRenderArea = CreateWindowExW(0, L"CuttyTermRenderArea", nullptr,
                                       WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL,
                                       0, 0, 0, 0, mHwnd, nullptr, mInstance, this);
    }

    LRESULT CALLBACK MainWindow::RenderAreaStaticProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        MainWindow* self;
        if (msg == WM_NCCREATE)
        {
            auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = reinterpret_cast<MainWindow*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        else
        {
            self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        if (self) return self->RenderAreaProc(hwnd, msg, wParam, lParam);
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    LRESULT MainWindow::RenderAreaProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_SIZE:
            OnRenderAreaSize(LOWORD(lParam), HIWORD(lParam));
            return 0;
        case WM_PAINT:
            OnPaint();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_LBUTTONDOWN:
            OnSelectionMouseDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_MOUSEMOVE:
            if (mSelecting) OnSelectionMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_LBUTTONUP:
            if (mSelecting) OnSelectionMouseUp();
            return 0;
        case WM_CAPTURECHANGED:
            if (mSelecting && reinterpret_cast<HWND>(lParam) != hwnd) OnSelectionMouseUp();
            return 0;
        case WM_TIMER:
            if (wParam == kSelectionScrollTimer) OnSelectionAutoScroll();
            return 0;
        case WM_VSCROLL:
        {
            TabState* tab = ActiveTab();
            if (!tab) return 0;
            int maxOffset = std::max(0, tab->buffer.TotalScrollableLines() - tab->buffer.Rows());
            int offset = tab->scrollOffset;
            switch (LOWORD(wParam))
            {
            case SB_LINEUP:   offset += 1; break;
            case SB_LINEDOWN: offset -= 1; break;
            case SB_PAGEUP:   offset += tab->buffer.Rows(); break;
            case SB_PAGEDOWN: offset -= tab->buffer.Rows(); break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION:
                // 스크롤바는 "맨 위=0, 맨 아래=maxOffset" 이고 scrollOffset 은 반대(맨 아래=0)이므로 뒤집는다.
                offset = maxOffset - static_cast<int>(HIWORD(wParam));
                break;
            default: break;
            }
            tab->scrollOffset = std::clamp(offset, 0, maxOffset);
            UpdateScrollBar();
            InvalidateTerminalArea();
            return 0;
        }
        case WM_HSCROLL:
        {
            TabState* tab = ActiveTab();
            if (!tab) return 0;
            int maxOffset = std::max(0, tab->buffer.LogicalColumns() - tab->buffer.Columns());
            int offset = tab->horizontalScrollOffset;
            switch (LOWORD(wParam))
            {
            case SB_LINELEFT:  offset -= 1; break;
            case SB_LINERIGHT: offset += 1; break;
            case SB_PAGELEFT:  offset -= tab->buffer.Columns(); break;
            case SB_PAGERIGHT: offset += tab->buffer.Columns(); break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION:
                offset = static_cast<int>(HIWORD(wParam)); // 가로는 위치 그대로(왼쪽=0)
                break;
            default: break;
            }
            tab->horizontalScrollOffset = std::clamp(offset, 0, maxOffset);
            UpdateScrollBar();
            InvalidateTerminalArea();
            return 0;
        }
        default:
            break;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    void MainWindow::OnRenderAreaSize(int width, int height)
    {
        if (width == 0 || height == 0 || !mD2DContext.DeviceContext()) return;
        mD2DContext.Resize(static_cast<UINT>(width), static_cast<UINT>(height));
        ResizeAllTabBuffers();
        InvalidateRect(mRenderArea, nullptr, FALSE);
    }

    void MainWindow::CreateToolbar()
    {
        mToolbar = CreateWindowExW(0, TOOLBARCLASSNAMEW, nullptr,
                                    WS_CHILD | WS_VISIBLE | TBSTYLE_TOOLTIPS | CCS_NODIVIDER,
                                    0, 0, 0, 0, mHwnd, nullptr, mInstance, nullptr);
        SendMessageW(mToolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);

        HIMAGELIST imageList = ImageList_Create(kToolbarIconSize, kToolbarIconSize, ILC_COLOR32, 3, 0);
        HICON icons[3] = { CreateToolbarIcon(DrawNewConnectionGlyph),
                           CreateToolbarIcon(DrawOptionsGlyph),
                           CreateToolbarIcon(DrawCloseGlyph) };
        for (HICON icon : icons)
        {
            ImageList_AddIcon(imageList, icon);
            DestroyIcon(icon);
        }
        SendMessageW(mToolbar, TB_SETIMAGELIST, 0, reinterpret_cast<LPARAM>(imageList));

        auto addString = [this](const wchar_t* text) -> int
        {
            return static_cast<int>(SendMessageW(mToolbar, TB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text)));
        };

        // 문자열은 여전히 등록한다 - 화면에는 안 보이지만(TB_SETMAXTEXTROWS 0) 마우스
        // 오버레이 시 툴팁으로는 계속 쓰인다.
        TBBUTTON buttons[3] = {};
        buttons[0] = { 0, ID_TOOLBAR_NEWCONNECTION, TBSTATE_ENABLED, TBSTYLE_BUTTON, {}, 0, addString(L"새 연결 (Telnet/SSH)") };
        buttons[1] = { 1, ID_TOOLBAR_OPTIONS, TBSTATE_ENABLED, TBSTYLE_BUTTON, {}, 0, addString(L"옵션") };
        buttons[2] = { 2, ID_TOOLBAR_CLOSETAB, TBSTATE_ENABLED, TBSTYLE_BUTTON, {}, 0, addString(L"현재 탭 닫기") };

        SendMessageW(mToolbar, TB_ADDBUTTONSW, 3, reinterpret_cast<LPARAM>(buttons));

        // 아이콘만 작게 보이고 설명 텍스트는 안 보이도록 함(요구사항: "아이콘을 작게
        // 표시하고 아이콘 설명은 제거"). 텍스트 행 수를 0으로 고정하면 라벨은 사라지고
        // TBSTYLE_TOOLTIPS 를 통한 호버 툴팁은 그대로 동작한다.
        SendMessageW(mToolbar, TB_SETMAXTEXTROWS, 0, 0);
        SendMessageW(mToolbar, TB_SETPADDING, 0, MAKELPARAM(6, 6));
        SendMessageW(mToolbar, TB_AUTOSIZE, 0, 0);

        // 실제 렌더링된 높이를 측정해 레이아웃 계산에 사용 (DPI/테마에 따라 달라질 수 있음).
        RECT toolbarRect;
        GetWindowRect(mToolbar, &toolbarRect);
        mToolbarHeight = toolbarRect.bottom - toolbarRect.top;
    }

    void MainWindow::CreateTabControl()
    {
        // 탭 줄 전체를 서브클래싱해 WM_PAINT 에서 직접 그린다 (둥근 탭, 호버 강조, 연결 상태 점).
        // 탭 추가/삭제/선택/히트테스트 같은 동작은 기본 SysTabControl32 에 그대로 맡긴다.
        mTabControl = CreateWindowExW(0, WC_TABCONTROLW, nullptr,
                                       WS_CHILD | WS_VISIBLE | TCS_TABS | TCS_FOCUSNEVER | TCS_FIXEDWIDTH,
                                       0, 0, 0, 0, mHwnd, nullptr, mInstance, nullptr);
        SendMessageW(mTabControl, TCM_SETITEMSIZE, 0, MAKELPARAM(kTabWidth, mTabHeight - 4));
        mTabFont = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        SendMessageW(mTabControl, WM_SETFONT, reinterpret_cast<WPARAM>(mTabFont), FALSE);
        SetWindowSubclass(mTabControl, TabControlSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));
    }

    LRESULT CALLBACK MainWindow::TabControlSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                                        UINT_PTR /*id*/, DWORD_PTR refData)
    {
        auto self = reinterpret_cast<MainWindow*>(refData);
        switch (msg)
        {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
        {
            // 깜빡임 방지를 위해 메모리 DC 에 전부 그린 뒤 한 번에 복사한다.
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT client;
            GetClientRect(hwnd, &client);
            HDC memDc = CreateCompatibleDC(dc);
            HBITMAP memBmp = CreateCompatibleBitmap(dc, client.right, client.bottom);
            HBITMAP oldBmp = static_cast<HBITMAP>(SelectObject(memDc, memBmp));
            self->PaintTabStrip(memDc, client);
            BitBlt(dc, 0, 0, client.right, client.bottom, memDc, 0, 0, SRCCOPY);
            SelectObject(memDc, oldBmp);
            DeleteObject(memBmp);
            DeleteDC(memDc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_MOUSEMOVE:
        {
            POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            TCHITTESTINFO hit{ pt, 0 };
            int index = TabCtrl_HitTest(hwnd, &hit);
            bool overClose = index >= 0 && self->HitTabCloseButton(index, pt);
            if (index != self->mHoverTabIndex || overClose != self->mHoverOverClose)
            {
                self->mHoverTabIndex = index;
                self->mHoverOverClose = overClose;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            TRACKMOUSEEVENT track{ sizeof(track), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&track);
            break;
        }
        case WM_MOUSELEAVE:
            self->mHoverTabIndex = -1;
            self->mHoverOverClose = false;
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        case WM_LBUTTONDOWN:
        case WM_MBUTTONUP:
        {
            // X 버튼 클릭 또는 가운데 버튼 클릭이면 그 탭을 닫는다 (탭 선택으로 넘기지 않음).
            POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            TCHITTESTINFO hit{ pt, 0 };
            int index = TabCtrl_HitTest(hwnd, &hit);
            if (index >= 0 && (msg == WM_MBUTTONUP || self->HitTabCloseButton(index, pt)))
            {
                self->mHoverTabIndex = -1;
                self->mHoverOverClose = false;
                self->RequestCloseTab(index);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            break;
        }
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, TabControlSubclassProc, 1);
            break;
        default:
            break;
        }
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    }

    RECT MainWindow::TabBodyRect(int index) const
    {
        // 화면에 그려지는 탭 모양의 영역 (위쪽 여백을 빼고 탭 줄 바닥까지).
        RECT item, client;
        TabCtrl_GetItemRect(mTabControl, index, &item);
        GetClientRect(mTabControl, &client);
        bool isSelected = (index == TabCtrl_GetCurSel(mTabControl));
        return RECT{ item.left + 2, item.top + (isSelected ? 2 : 4), item.right - 2, client.bottom };
    }

    RECT MainWindow::TabCloseButtonRect(int index) const
    {
        RECT body = TabBodyRect(index);
        int centerY = (body.top + body.bottom) / 2;
        return RECT{ body.right - 24, centerY - 8, body.right - 8, centerY + 8 };
    }

    bool MainWindow::HitTabCloseButton(int index, POINT pt) const
    {
        RECT close = TabCloseButtonRect(index);
        return PtInRect(&close, pt) != FALSE;
    }

    void MainWindow::PaintTabStrip(HDC dc, const RECT& client)
    {
        using namespace Gdiplus;

        Graphics g(dc);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

        // 탭 줄 배경은 툴바와 어울리는 밝은 회색, 맨 아래에 얇은 경계선.
        SolidBrush stripBrush(Color(255, 232, 232, 235));
        g.FillRectangle(&stripBrush, 0, 0, client.right, client.bottom);
        Pen baseLine(Color(255, 170, 170, 176), 1.0f);
        g.DrawLine(&baseLine, 0.0f, client.bottom - 0.5f, static_cast<float>(client.right), client.bottom - 0.5f);

        Font font(dc, mTabFont);
        StringFormat format;
        format.SetLineAlignment(StringAlignmentCenter);
        format.SetTrimming(StringTrimmingEllipsisCharacter);
        format.SetFormatFlags(StringFormatFlagsNoWrap);

        const int selected = TabCtrl_GetCurSel(mTabControl);
        const int count = TabCtrl_GetItemCount(mTabControl);
        for (int i = 0; i < count && i < static_cast<int>(mTabs.size()); ++i)
        {
            bool isSelected = (i == selected);
            bool isHover = (i == mHoverTabIndex);
            RECT body = TabBodyRect(i);
            float left = static_cast<float>(body.left);
            float right = static_cast<float>(body.right);
            float top = static_cast<float>(body.top);
            float bottom = static_cast<float>(body.bottom);

            // 위쪽 모서리만 둥근 탭 모양 (아래는 탭 줄 바닥까지 곧게 내려감).
            const float radius = 7.0f;
            GraphicsPath shape;
            shape.AddArc(left, top, radius * 2, radius * 2, 180.0f, 90.0f);
            shape.AddArc(right - radius * 2, top, radius * 2, radius * 2, 270.0f, 90.0f);
            shape.AddLine(right, top + radius, right, bottom);
            shape.AddLine(right, bottom, left, bottom);
            shape.CloseFigure();

            // 비활성 탭은 회색, 마우스를 올리면 조금 밝은 회색, 포커스된(선택된) 탭은 흰색.
            Color fill = isSelected ? Color(255, 255, 255, 255)
                                    : (isHover ? Color(255, 214, 214, 218) : Color(255, 196, 196, 201));
            SolidBrush fillBrush(fill);
            g.FillPath(&fillBrush, &shape);
            Pen edge(Color(255, 160, 160, 166), 1.0f);
            g.DrawPath(&edge, &shape);

            if (isSelected)
            {
                // 선택된 탭 위쪽에 파란 강조선 (탭 모양에 맞춰 잘라서 둥근 모서리를 따라가게 함).
                g.SetClip(&shape);
                LinearGradientBrush accent(PointF(left, 0), PointF(right, 0), Color(255, 0, 122, 204), Color(255, 80, 170, 255));
                g.FillRectangle(&accent, left, top, right - left, 2.5f);
                g.ResetClip();
            }

            // 연결 상태 점 (초록=연결됨, 회색=연결 중/끊김)
            const TabState& tab = *mTabs[i];
            bool connected = tab.connection && !tab.closed;
            SolidBrush dot(connected ? Color(255, 40, 170, 80) : Color(255, 130, 130, 136));
            float midY = (top + bottom) / 2.0f;
            g.FillEllipse(&dot, left + 10.0f, midY - 3.5f, 7.0f, 7.0f);

            SolidBrush textBrush(Color(255, 0, 0, 0));
            RectF textRect(left + 23.0f, top, right - left - 52.0f, bottom - top);
            g.DrawString(tab.title.c_str(), -1, &font, textRect, &format, &textBrush);

            // 닫기 X 버튼: 마우스를 올리면 빨간 원 배경 + 흰 X 로 강조.
            RECT closeRect = TabCloseButtonRect(i);
            RectF close(static_cast<float>(closeRect.left), static_cast<float>(closeRect.top), 16.0f, 16.0f);
            bool closeHot = isHover && mHoverOverClose;
            if (closeHot)
            {
                SolidBrush hot(Color(255, 220, 60, 60));
                g.FillEllipse(&hot, close);
            }
            Pen cross(closeHot ? Color(255, 255, 255, 255) : Color(255, 70, 70, 76), 1.5f);
            cross.SetStartCap(LineCapRound);
            cross.SetEndCap(LineCapRound);
            g.DrawLine(&cross, close.X + 5.0f, close.Y + 5.0f, close.X + 11.0f, close.Y + 11.0f);
            g.DrawLine(&cross, close.X + 11.0f, close.Y + 5.0f, close.X + 5.0f, close.Y + 11.0f);
        }
    }

    void MainWindow::LayoutChildren()
    {
        RECT client;
        GetClientRect(mHwnd, &client);
        int width = client.right - client.left;
        int height = client.bottom - client.top;
        MoveWindow(mToolbar, 0, 0, width, mToolbarHeight, TRUE);
        MoveWindow(mTabControl, 0, mToolbarHeight, width, mTabHeight, TRUE);

        RECT area = TerminalAreaRect();
        MoveWindow(mRenderArea, area.left, area.top, area.right - area.left,
                   std::max(0, height - (mToolbarHeight + mTabHeight)), TRUE);
    }

    RECT MainWindow::TerminalAreaRect() const
    {
        RECT client;
        GetClientRect(mHwnd, &client);
        RECT area = client;
        area.top = mToolbarHeight + mTabHeight;
        return area;
    }

    void MainWindow::OnSize(int width, int height)
    {
        if (width == 0 || height == 0) return;
        LayoutChildren(); // mRenderArea 크기 변경 -> 그 자식 윈도우의 WM_SIZE 에서 D2D/버퍼 갱신 처리
    }

    void MainWindow::RecomputeGridSize(int* outCols, int* outRows) const
    {
        // mRenderArea 자체의 클라이언트 영역을 써야 한다 (WS_VSCROLL 이 있으면
        // 스크롤바 폭만큼 실제 표시 영역이 TerminalAreaRect() 보다 좁아지는데,
        // 예전에는 프레임 기준 크기를 써서 렌더링 폭과 컬럼 수가 어긋나
        // 스크롤바/텍스트가 겹쳐 보이는 문제가 있었다).
        RECT area;
        GetClientRect(mRenderArea, &area);
        int w = area.right - area.left;
        int h = area.bottom - area.top;
        float cw = mTextRenderer.CellWidth();
        float ch = mTextRenderer.CellHeight();
        *outCols = std::max(1, static_cast<int>(w / (cw > 0 ? cw : 1)));
        *outRows = std::max(1, static_cast<int>(h / (ch > 0 ? ch : 1)));
    }

    void MainWindow::ResizeAllTabBuffers()
    {
        int cols, rows;
        RecomputeGridSize(&cols, &rows);
        for (auto& tab : mTabs)
        {
            tab->buffer.Resize(cols, rows);
            if (tab->connection) tab->connection->Resize(cols, rows);

            int maxOffset = std::max(0, tab->buffer.TotalScrollableLines() - tab->buffer.Rows());
            tab->scrollOffset = std::clamp(tab->scrollOffset, 0, maxOffset);

            int maxH = std::max(0, tab->buffer.LogicalColumns() - tab->buffer.Columns());
            tab->horizontalScrollOffset = std::clamp(tab->horizontalScrollOffset, 0, maxH);
        }
        UpdateScrollBar();
    }

    void MainWindow::InvalidateTerminalArea()
    {
        InvalidateRect(mRenderArea, nullptr, FALSE);
    }

    void MainWindow::OnPaint()
    {
        // 스크롤바 갱신은 여기 한 곳에서만 한다 (데이터가 올 때마다 매번 갱신하면
        // 출력이 몰릴 때 비-클라이언트 영역이 과도하게 다시 그려져 스크롤바가
        // 깜빡이거나 이상하게 보일 수 있다). WM_PAINT 는 여러 Invalidate 요청을
        // 하나로 묶어 처리하므로 빈도가 자연스럽게 제한된다.
        UpdateScrollBar();

        PAINTSTRUCT ps;
        BeginPaint(mRenderArea, &ps);

        if (mD2DContext.DeviceContext())
        {
            mD2DContext.BeginDraw();

            TabState* tab = ActiveTab();
            if (tab)
            {
                ImeOverlay overlay;
                bool hasOverlay = mIme.IsComposing();
                if (hasOverlay) overlay.text = mIme.Text();

                TextSelection selection;
                if (tab->hasSelection) selection = NormalizedSelection(*tab);

                mTextRenderer.Render(tab->buffer, tab->scrollOffset, tab->horizontalScrollOffset, mLiveBackground,
                                      tab->buffer.IsCursorVisible(), GetFocus() == mHwnd, hasOverlay ? &overlay : nullptr,
                                      tab->hasSelection ? &selection : nullptr);
            }
            else
            {
                mD2DContext.DeviceContext()->Clear(D2D1::ColorF(mLiveBackground, 1.0f));
            }

            mD2DContext.EndDraw(true);
        }

        EndPaint(mRenderArea, &ps);
    }

    bool MainWindow::OnKeyDown(WPARAM key, bool alt)
    {
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

        if (!alt)
        {
            // Ctrl+C: 선택 영역이 있으면 복사, 없으면 평소처럼 ^C(인터럽트)를 원격으로 보낸다.
            // Ctrl+Shift+C 는 항상 복사. 뒤따르는 WM_CHAR(0x03) 는 복사했을 때만 무시한다.
            if (ctrl && key == 'C')
            {
                TabState* tab = ActiveTab();
                if (shift || (tab && tab->hasSelection))
                {
                    CopySelectionToClipboard();
                    mSuppressNextChar = true;
                }
                return true;
            }
            // Ctrl+V / Ctrl+Shift+V: 붙여넣기 (뒤따르는 WM_CHAR 0x16 은 무시).
            if (ctrl && key == 'V')
            {
                PasteFromClipboard();
                mSuppressNextChar = true;
                return true;
            }
            // Shift+Insert: 붙여넣기 (WM_CHAR 가 생기지 않으므로 억제 불필요).
            if (shift && !ctrl && key == VK_INSERT)
            {
                PasteFromClipboard();
                return true;
            }
            // Shift+PgUp/PgDn: PuTTY 처럼 원격으로 보내지 않고 스크롤백을 한 화면씩 넘긴다.
            if (shift && !ctrl && (key == VK_PRIOR || key == VK_NEXT))
            {
                ScrollViewByPage(key == VK_PRIOR ? 1 : -1);
                return true;
            }
            // Shift+Tab: 역방향 탭 (뒤따르는 WM_CHAR '\t' 는 무시)
            if (shift && !ctrl && key == VK_TAB)
            {
                SendKeystrokeBytes("\x1b[Z");
                mSuppressNextChar = true;
                return true;
            }
        }

        // xterm 수정키 파라미터: 1 + Shift(1) + Alt(2) + Ctrl(4). 1 이면 수정키 없음.
        const int modifier = 1 + (shift ? 1 : 0) + (alt ? 2 : 0) + (ctrl ? 4 : 0);
        auto tildeKey = [&](int code) // Insert/Delete/PgUp/PgDn/F5~F12: ESC [ code ~ 또는 ESC [ code ; m ~
        {
            std::string seq = "\x1b[" + std::to_string(code);
            if (modifier > 1) seq += ";" + std::to_string(modifier);
            SendKeystrokeBytes(seq + "~");
        };
        auto letterKey = [&](char finalChar, bool cursorKey) // 방향키/Home/End/F1~F4
        {
            if (modifier > 1)
            {
                SendKeystrokeBytes("\x1b[1;" + std::to_string(modifier) + finalChar);
            }
            else if (cursorKey)
            {
                // DECCKM 모드에 따라 ESC [ x 또는 ESC O x (모드를 무시하면 zsh 등에서 Home/End 가 안 먹음)
                SendCursorKey(finalChar);
            }
            else
            {
                SendKeystrokeBytes(std::string("\x1bO") + finalChar);
            }
        };

        switch (key)
        {
        case VK_BACK:
            // Backspace=DEL(0x7f), Ctrl+Backspace=^H, Alt+Backspace=ESC DEL (단어 지우기)
            if (ctrl) SendKeystrokeBytes("\x08");
            else SendKeystrokeBytes(alt ? "\x1b\x7f" : "\x7f");
            mSuppressNextChar = true;
            return true;
        case VK_UP:    letterKey('A', true); return true;
        case VK_DOWN:  letterKey('B', true); return true;
        case VK_RIGHT: letterKey('C', true); return true;
        case VK_LEFT:  letterKey('D', true); return true;
        case VK_HOME:  letterKey('H', true); return true;
        case VK_END:   letterKey('F', true); return true;
        case VK_F1:    letterKey('P', false); return true;
        case VK_F2:    letterKey('Q', false); return true;
        case VK_F3:    letterKey('R', false); return true;
        case VK_F4:    letterKey('S', false); return true;
        case VK_INSERT: tildeKey(2); return true;
        case VK_DELETE: tildeKey(3); return true;
        case VK_PRIOR:  tildeKey(5); return true;
        case VK_NEXT:   tildeKey(6); return true;
        case VK_F5:  tildeKey(15); return true;
        case VK_F6:  tildeKey(17); return true;
        case VK_F7:  tildeKey(18); return true;
        case VK_F8:  tildeKey(19); return true;
        case VK_F9:  tildeKey(20); return true;
        case VK_F10: tildeKey(21); return true;
        case VK_F11: tildeKey(23); return true;
        case VK_F12: tildeKey(24); return true;
        default: return false; // 일반 글자는 WM_CHAR / WM_SYSCHAR 에서 처리
        }
    }

    void MainWindow::OnChar(wchar_t ch, bool alt)
    {
        if (mSuppressNextChar)
        {
            mSuppressNextChar = false;
            return;
        }
        TabState* tab = ActiveTab();
        if (!tab) return;

        if (ch >= 0xD800 && ch <= 0xDBFF) { mPendingHighSurrogate = ch; return; }

        std::wstring text(1, ch);
        if (ch >= 0xDC00 && ch <= 0xDFFF && mPendingHighSurrogate)
        {
            text.insert(text.begin(), mPendingHighSurrogate);
            mPendingHighSurrogate = 0;
        }

        // Ctrl+Space 는 NUL (emacs 의 set-mark 등). Windows 는 그냥 공백을 준다.
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        std::string bytes = (ctrl && !alt && ch == L' ') ? std::string(1, '\0')
                                                         : tab->decoder.Encode(text); // 연결의 인코딩(UTF-8/CP949 등)으로
        if (alt) bytes.insert(0, "\x1b"); // Alt+글자 = ESC 접두 (bash/emacs 의 Meta 키)
        SendKeystrokeBytes(bytes);
    }

    void MainWindow::SendCursorKey(char finalChar)
    {
        TabState* tab = ActiveTab();
        if (!tab) return;
        bool application = tab->parser.ApplicationCursorKeys();
        SendKeystrokeBytes(std::string("\x1b") + (application ? 'O' : '[') + finalChar);
    }

    void MainWindow::ScrollViewByPage(int direction)
    {
        TabState* tab = ActiveTab();
        if (!tab) return;
        int maxOffset = std::max(0, tab->buffer.TotalScrollableLines() - tab->buffer.Rows());
        tab->scrollOffset = std::clamp(tab->scrollOffset + direction * std::max(1, tab->buffer.Rows() - 1), 0, maxOffset);
        UpdateScrollBar();
        InvalidateTerminalArea();
    }

    void MainWindow::SendKeystrokeBytes(const std::string& bytes)
    {
        TabState* tab = ActiveTab();
        if (!tab) return;
        if (tab->hasSelection)
        {
            tab->hasSelection = false; // 입력하면 선택 해제 (일반적인 터미널 동작)
            InvalidateTerminalArea();
        }
        // 사용자가 입력하면 항상 최신 출력을 볼 수 있도록 스크롤을 맨 아래/왼쪽으로 되돌린다.
        if (tab->scrollOffset != 0 || tab->horizontalScrollOffset != 0)
        {
            tab->scrollOffset = 0;
            tab->horizontalScrollOffset = 0;
            UpdateScrollBar();
            InvalidateTerminalArea();
        }
        if (tab->connection) tab->connection->Send(bytes);
    }

    void MainWindow::HitTestCell(TabState& tab, int x, int y, int64_t* outLine, int* outColumn) const
    {
        float cw = mTextRenderer.CellWidth();
        float ch = mTextRenderer.CellHeight();
        int row = static_cast<int>(std::floor(y / (ch > 0 ? ch : 1)));
        int column = static_cast<int>(std::floor(x / (cw > 0 ? cw : 1))) + tab.horizontalScrollOffset;
        row = std::clamp(row, 0, tab.buffer.Rows() - 1);
        *outColumn = std::clamp(column, 0, tab.buffer.LogicalColumns() - 1);
        *outLine = tab.buffer.ViewLineId(row, tab.scrollOffset);
    }

    void MainWindow::OnSelectionMouseDown(int x, int y)
    {
        SetFocus(mHwnd); // 키보드 입력은 항상 프레임 윈도우가 받는다
        TabState* tab = ActiveTab();
        if (!tab) return;

        HitTestCell(*tab, x, y, &tab->selectionAnchorLine, &tab->selectionAnchorColumn);
        tab->selectionEndLine = tab->selectionAnchorLine;
        tab->selectionEndColumn = tab->selectionAnchorColumn;
        tab->hasSelection = false; // 단순 클릭은 기존 선택 해제, 실제로 드래그해야 선택이 생김
        mSelecting = true;
        SetCapture(mRenderArea);
        SetTimer(mRenderArea, kSelectionScrollTimer, 50, nullptr);
        InvalidateTerminalArea();
    }

    void MainWindow::OnSelectionMouseMove(int x, int y)
    {
        TabState* tab = ActiveTab();
        if (!tab) return;

        HitTestCell(*tab, x, y, &tab->selectionEndLine, &tab->selectionEndColumn);
        if (tab->selectionEndLine != tab->selectionAnchorLine || tab->selectionEndColumn != tab->selectionAnchorColumn)
        {
            tab->hasSelection = true;
        }
        InvalidateTerminalArea();
    }

    void MainWindow::OnSelectionMouseUp()
    {
        mSelecting = false;
        KillTimer(mRenderArea, kSelectionScrollTimer);
        if (GetCapture() == mRenderArea) ReleaseCapture();
    }

    void MainWindow::OnSelectionAutoScroll()
    {
        TabState* tab = ActiveTab();
        if (!mSelecting || !tab) return;

        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(mRenderArea, &pt);
        RECT client;
        GetClientRect(mRenderArea, &client);

        int maxOffset = std::max(0, tab->buffer.TotalScrollableLines() - tab->buffer.Rows());
        int maxH = std::max(0, tab->buffer.LogicalColumns() - tab->buffer.Columns());
        int oldV = tab->scrollOffset;
        int oldH = tab->horizontalScrollOffset;
        if (pt.y < client.top) tab->scrollOffset = std::min(maxOffset, tab->scrollOffset + 1);
        else if (pt.y >= client.bottom) tab->scrollOffset = std::max(0, tab->scrollOffset - 1);
        if (pt.x < client.left) tab->horizontalScrollOffset = std::max(0, tab->horizontalScrollOffset - 1);
        else if (pt.x >= client.right) tab->horizontalScrollOffset = std::min(maxH, tab->horizontalScrollOffset + 1);

        if (oldV != tab->scrollOffset || oldH != tab->horizontalScrollOffset)
        {
            OnSelectionMouseMove(pt.x, pt.y);
        }
    }

    TextSelection MainWindow::NormalizedSelection(const TabState& tab)
    {
        bool anchorFirst = tab.selectionAnchorLine < tab.selectionEndLine ||
                           (tab.selectionAnchorLine == tab.selectionEndLine && tab.selectionAnchorColumn <= tab.selectionEndColumn);
        TextSelection selection;
        selection.startLine = anchorFirst ? tab.selectionAnchorLine : tab.selectionEndLine;
        selection.startColumn = anchorFirst ? tab.selectionAnchorColumn : tab.selectionEndColumn;
        selection.endLine = anchorFirst ? tab.selectionEndLine : tab.selectionAnchorLine;
        selection.endColumn = anchorFirst ? tab.selectionEndColumn : tab.selectionAnchorColumn;
        return selection;
    }

    std::wstring MainWindow::SelectedText(const TabState& tab)
    {
        if (!tab.hasSelection) return {};
        TextSelection selection = NormalizedSelection(tab);

        std::wstring result;
        for (int64_t lineId = selection.startLine; lineId <= selection.endLine; ++lineId)
        {
            std::wstring text;
            if (const std::vector<Cell>* line = tab.buffer.LineById(lineId))
            {
                int first = (lineId == selection.startLine) ? selection.startColumn : 0;
                int last = (lineId == selection.endLine) ? selection.endColumn : static_cast<int>(line->size()) - 1;
                last = std::min(last, static_cast<int>(line->size()) - 1);
                for (int col = first; col <= last; ++col)
                {
                    const Cell& cell = (*line)[col];
                    if (cell.isTrailing) continue; // 2칸 문자의 두번째 칸
                    char32_t cp = cell.codepoint ? cell.codepoint : U' ';
                    if (cp >= 0x10000)
                    {
                        text.push_back(static_cast<wchar_t>(0xD800 + ((cp - 0x10000) >> 10)));
                        text.push_back(static_cast<wchar_t>(0xDC00 + ((cp - 0x10000) & 0x3FF)));
                    }
                    else
                    {
                        text.push_back(static_cast<wchar_t>(cp));
                    }
                }
            }
            // 줄 끝의 빈 칸(터미널 그리드 여백)은 복사하지 않는다.
            while (!text.empty() && text.back() == L' ') text.pop_back();
            result += text;
            if (lineId != selection.endLine) result += L"\r\n";
        }
        return result;
    }

    bool MainWindow::CopySelectionToClipboard()
    {
        TabState* tab = ActiveTab();
        if (!tab || !tab->hasSelection) return false;
        std::wstring text = SelectedText(*tab);
        if (!OpenClipboard(mHwnd)) return false;

        EmptyClipboard();
        size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        bool ok = false;
        if (HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes))
        {
            memcpy(GlobalLock(memory), text.c_str(), bytes);
            GlobalUnlock(memory);
            ok = SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
            if (!ok) GlobalFree(memory); // 성공하면 소유권이 클립보드로 넘어감
        }
        CloseClipboard();

        // 복사 후 선택 해제 - 복사되었다는 시각적 피드백 (Windows Terminal 과 같은 동작).
        tab->hasSelection = false;
        InvalidateTerminalArea();
        return ok;
    }

    void MainWindow::PasteFromClipboard()
    {
        TabState* tab = ActiveTab();
        if (!tab || !OpenClipboard(mHwnd)) return;

        std::wstring text;
        if (HANDLE data = GetClipboardData(CF_UNICODETEXT))
        {
            if (auto source = static_cast<const wchar_t*>(GlobalLock(data)))
            {
                text = source;
                GlobalUnlock(data);
            }
        }
        CloseClipboard();
        if (text.empty()) return;

        // 줄바꿈은 터미널의 Enter 와 같은 CR 하나로 통일한다 (CRLF/LF -> CR).
        std::wstring normalized;
        normalized.reserve(text.size());
        for (size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] == L'\n' || text[i] == L'\r')
            {
                normalized.push_back(L'\r');
                if (text[i] == L'\r' && i + 1 < text.size() && text[i + 1] == L'\n') ++i;
            }
            else
            {
                normalized.push_back(text[i]);
            }
        }

        std::string bytes = tab->decoder.Encode(normalized); // 연결의 인코딩(UTF-8/CP949 등)으로
        if (tab->parser.BracketedPaste())
        {
            // 셸이 bracketed paste 를 켰으면 ESC[200~ ... ESC[201~ 로 감싸, 여러 줄을 붙여넣어도
            // 줄마다 바로 실행되지 않게 한다. 내용 안의 종료 표식은 제거(조기 종료 방지).
            const std::string endMarker = "\x1b[201~";
            for (size_t pos; (pos = bytes.find(endMarker)) != std::string::npos;) bytes.erase(pos, endMarker.size());
            bytes = "\x1b[200~" + bytes + endMarker;
        }
        SendKeystrokeBytes(bytes);
    }

    void MainWindow::OnMouseWheel(int wheelDelta)
    {
        TabState* tab = ActiveTab();
        if (!tab) return;

        constexpr int kLinesPerNotch = 3;
        int notches = wheelDelta / WHEEL_DELTA;

        // Shift+휠: 가로 스크롤 (엑셀/대부분의 앱과 동일한 관례).
        if (GetKeyState(VK_SHIFT) & 0x8000)
        {
            int maxH = std::max(0, tab->buffer.LogicalColumns() - tab->buffer.Columns());
            tab->horizontalScrollOffset = std::clamp(tab->horizontalScrollOffset + notches * kLinesPerNotch, 0, maxH);
        }
        else
        {
            int maxOffset = std::max(0, tab->buffer.TotalScrollableLines() - tab->buffer.Rows());
            tab->scrollOffset = std::clamp(tab->scrollOffset + notches * kLinesPerNotch, 0, maxOffset);
        }
        if (mSelecting)
        {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(mRenderArea, &pt);
            OnSelectionMouseMove(pt.x, pt.y);
        }
        UpdateScrollBar();
        InvalidateTerminalArea();
    }

    void MainWindow::UpdateScrollBar()
    {
        TabState* tab = ActiveTab();
        if (!tab)
        {
            SCROLLINFO clearInfo{ sizeof(SCROLLINFO), SIF_RANGE, 0, 0, 0, 0, 0 };
            SetScrollInfo(mRenderArea, SB_VERT, &clearInfo, TRUE);
            SetScrollInfo(mRenderArea, SB_HORZ, &clearInfo, TRUE);
            return;
        }

        int totalLines = tab->buffer.TotalScrollableLines();
        int rows = tab->buffer.Rows();
        int maxOffset = std::max(0, totalLines - rows);

        // scrollOffset(0=맨 아래) 을 스크롤바 위치(0=맨 위)로 뒤집어서 반영.
        // SIF_DISABLENOSCROLL 을 주지 않으면, 스크롤백이 화면 한 장 이하일 때
        // (스크롤할 내용이 없을 때) Windows 가 자동으로 비활성(회색) 상태로
        // 표시해준다 — 매번 꽉 찬 썸으로 보이는 것보다 더 자연스럽다.
        SCROLLINFO info{};
        info.cbSize = sizeof(SCROLLINFO);
        info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        info.nMin = 0;
        info.nMax = std::max(0, totalLines - 1);
        info.nPage = static_cast<UINT>(rows);
        info.nPos = maxOffset - tab->scrollOffset;
        SetScrollInfo(mRenderArea, SB_VERT, &info, TRUE);

        // 가로 스크롤바: 창이 예전보다 좁아져 LogicalColumns() > Columns() 일 때만 움직일 내용이 있음.
        int logicalCols = tab->buffer.LogicalColumns();
        int viewportCols = tab->buffer.Columns();
        int maxH = std::max(0, logicalCols - viewportCols);
        SCROLLINFO hInfo{};
        hInfo.cbSize = sizeof(SCROLLINFO);
        hInfo.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        hInfo.nMin = 0;
        hInfo.nMax = std::max(0, logicalCols - 1);
        hInfo.nPage = static_cast<UINT>(viewportCols);
        hInfo.nPos = tab->horizontalScrollOffset;
        SetScrollInfo(mRenderArea, SB_HORZ, &hInfo, TRUE);
    }

    void MainWindow::OnCommand(WPARAM wParam)
    {
        switch (LOWORD(wParam))
        {
        case ID_TOOLBAR_NEWCONNECTION: OpenNewConnection(); break;
        case ID_TOOLBAR_OPTIONS: ShowOptions(); break;
        case ID_TOOLBAR_CLOSETAB:
            if (mActiveTabIndex >= 0) RequestCloseTab(mActiveTabIndex);
            break;
        case ID_ACCEL_NEXTTAB: SwitchTabRelative(1); break;
        case ID_ACCEL_PREVTAB: SwitchTabRelative(-1); break;
        default: break;
        }
    }

    void MainWindow::OnNotify(LPARAM lParam)
    {
        auto hdr = reinterpret_cast<NMHDR*>(lParam);
        if (hdr->hwndFrom == mTabControl && hdr->code == TCN_SELCHANGE)
        {
            mActiveTabIndex = TabCtrl_GetCurSel(mTabControl);
            InvalidateRect(mTabControl, nullptr, TRUE); // 어떤 탭이 활성인지 표시가 바뀌므로 탭줄도 다시 그림
            InvalidateTerminalArea();
            SetFocus(mHwnd);
        }
    }

    void MainWindow::ShowOptions()
    {
        bool changed = OptionsDialog::Show(mHwnd, mInstance, mConfig,
                                            [this](const AppConfig& preview) { ApplyPreviewConfig(preview); });
        if (changed) mConfig.Save();
    }

    void MainWindow::ApplyPreviewConfig(const AppConfig& preview)
    {
        mTextRenderer.SetFont(preview.fontFamily, preview.fontSize);

        for (auto& tab : mTabs)
        {
            // 이미 화면에 표시된 셀 중, "예전 기본색"과 정확히 일치하는 셀은 새
            // 기본색으로 같이 바꿔준다 - 그래야 옵션에서 색을 바꿀 때 이미 출력된
            // 내용도 반영된다(그냥 CurrentAttr 만 바꾸면 이후 새로 찍히는 글자만 바뀜).
            tab->buffer.RecolorDefaults(mLiveForeground, preview.foregroundColor,
                                         mLiveBackground, preview.backgroundColor);
            tab->buffer.SetDefaultColors(preview.foregroundColor, preview.backgroundColor);
            tab->buffer.CurrentAttr().foreground = preview.foregroundColor;
            tab->buffer.CurrentAttr().background = preview.backgroundColor;
        }
        mLiveForeground = preview.foregroundColor;
        mLiveBackground = preview.backgroundColor;

        ResizeAllTabBuffers();
        InvalidateTerminalArea();
    }

    void MainWindow::OpenNewConnection()
    {
        auto profile = ConnectionDialog::Show(mHwnd, mInstance, mConfig);
        // "추가/삭제" 만 하고 연결 없이 닫아도(취소) 목록 변경이 저장되도록,
        // 연결 여부와 상관없이 항상 저장한다.
        mConfig.Save();
        if (!profile) return;
        StartConnection(*profile);
    }

    void MainWindow::StartConnection(const ConnectionProfile& profileIn)
    {
        ConnectionProfile profile = profileIn;
        std::wstring username = profile.username;
        std::wstring password = profile.password;

        // 사용자명이 없을 때만 로그인 창을 먼저 띄운다. 비밀번호/키 암호/서버 질문(OTP 등)은
        // 서버가 실제로 요구할 때 인증 도중에 물어본다 (공개키만으로 들어가는 경우도 있으므로).
        if (profile.protocol == ProtocolType::Ssh && username.empty())
        {
            auto login = LoginDialog::Show(mHwnd, mInstance, profile.username);
            if (!login || login->username.empty()) return;
            username = login->username;
            if (password.empty()) password = login->password;
        }

        TabState* tab = AddTab(profile);
        int tabId = tab->id;
        HWND hwnd = mHwnd;
        HINSTANCE instance = mInstance;

        std::thread([hwnd, instance, tabId, profile, username, password]()
        {
            IConnection* raw = nullptr;
            if (profile.protocol == ProtocolType::Ssh)
            {
                auto ssh = new SshConnection();
                ssh->SetCredentials(username, password, profile.privateKeyFile);
                // 인증 중 입력이 필요하면 이 배경 스레드에서 바로 입력 창을 띄운다
                // (호스트 키 확인 MessageBox 와 같은 방식 - DialogBox 가 자체 메시지 루프를 돌림).
                ssh->OnAuthPrompt = [hwnd, instance](const std::wstring& title, const std::wstring& message, bool echo)
                {
                    return AuthPromptDialog::Show(hwnd, instance, title, message, echo);
                };
                // 최초 접속 시 호스트 키를 putty 처럼 사용자에게 확인받는다. Connect() 를
                // 호출 중인 이 배경 스레드에서 동기적으로 호출되므로, MessageBoxW 를 그냥
                // 직접 띄운다(어떤 스레드에서든 자체 메시지 루프를 돌리므로 안전하며,
                // hwnd 를 소유자로 지정하면 메인 창도 그동안 비활성화되어 모달처럼 동작한다).
                ssh->OnHostKeyPrompt = [hwnd](const std::wstring& host, const std::wstring& fingerprint,
                                              const std::wstring& keyType) -> bool
                {
                    std::wstring message =
                        L"이 서버의 호스트 키가 아직 캐시에 없습니다.\n\n"
                        L"호스트: " + host + L"\n"
                        L"키 종류: " + keyType + L"\n"
                        L"지문: " + fingerprint + L"\n\n"
                        L"이 서버를 신뢰하고 키를 캐시에 저장한 뒤 계속 진행하시겠습니까?\n"
                        L"(신뢰할 수 없는 네트워크라면 '아니오' 를 누르세요.)";
                    int result = MessageBoxW(hwnd, message.c_str(), L"보안 경고 - 알 수 없는 호스트 키",
                                              MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
                    return result == IDYES;
                };
                raw = ssh;
            }
            else
            {
                raw = new TelnetConnection();
            }

            raw->OnData = [hwnd, tabId](const uint8_t* data, size_t len)
            {
                auto copy = new std::vector<uint8_t>(data, data + len);
                PostMessageW(hwnd, kMsgConnData, static_cast<WPARAM>(tabId), reinterpret_cast<LPARAM>(copy));
            };
            raw->OnClosed = [hwnd, tabId]()
            {
                PostMessageW(hwnd, kMsgConnClosed, static_cast<WPARAM>(tabId), 0);
            };
            raw->OnError = [hwnd, tabId](const std::wstring& message)
            {
                auto copy = new std::wstring(message);
                PostMessageW(hwnd, kMsgConnError, static_cast<WPARAM>(tabId), reinterpret_cast<LPARAM>(copy));
            };

            if (raw->Connect(profile.host, profile.port))
            {
                PostMessageW(hwnd, kMsgConnReady, static_cast<WPARAM>(tabId), reinterpret_cast<LPARAM>(raw));
            }
            else
            {
                delete raw;
                PostMessageW(hwnd, kMsgConnClosed, static_cast<WPARAM>(tabId), 0);
            }
        }).detach();
    }

    MainWindow::TabState* MainWindow::AddTab(const ConnectionProfile& profile)
    {
        auto tab = std::make_unique<TabState>(mNextTabId++);
        tab->profile = profile;
        tab->decoder.SetCodePage(profile.codePage); // 연결별 인코딩 (0 = 자동 감지)
        tab->title = profile.name.empty() ? profile.host : profile.name;

        int cols, rows;
        RecomputeGridSize(&cols, &rows);
        tab->buffer.Resize(cols, rows);
        tab->buffer.SetDefaultColors(mConfig.foregroundColor, mConfig.backgroundColor);
        tab->buffer.CurrentAttr().foreground = mConfig.foregroundColor;
        tab->buffer.CurrentAttr().background = mConfig.backgroundColor;

        int tabId = tab->id;
        tab->parser.OnResponse = [this, tabId](const std::string& bytes)
        {
            if (TabState* t = FindTabById(tabId)) if (t->connection) t->connection->Send(bytes);
        };
        tab->parser.OnTitleChanged = [this, tabId](const std::wstring& title) { UpdateTabTitle(tabId, title); };
        tab->parser.OnBell = []() { MessageBeep(MB_OK); };

        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = const_cast<LPWSTR>(tab->title.c_str());
        int newIndex = static_cast<int>(mTabs.size());
        TabCtrl_InsertItem(mTabControl, newIndex, &item);

        TabState* ptr = tab.get();
        mTabs.push_back(std::move(tab));
        SwitchToTab(newIndex);
        return ptr;
    }

    void MainWindow::RequestCloseTab(int index)
    {
        if (index < 0 || index >= static_cast<int>(mTabs.size())) return;
        const TabState& tab = *mTabs[index];
        bool connected = tab.connection && !tab.closed;
        std::wstring message = L"'" + tab.title + L"' 탭을 닫으시겠습니까?";
        if (connected) message += L"\n\n현재 연결이 끊어집니다.";
        if (MessageBoxW(mHwnd, message.c_str(), L"탭 닫기", MB_YESNO | MB_ICONQUESTION) == IDYES)
        {
            CloseTab(index);
        }
        SetFocus(mHwnd); // 메시지 상자가 닫힌 뒤 키보드 입력이 다시 터미널로 가도록
    }

    void MainWindow::CloseTab(int index)
    {
        if (index < 0 || index >= static_cast<int>(mTabs.size())) return;
        mTabs.erase(mTabs.begin() + index);
        TabCtrl_DeleteItem(mTabControl, index);

        if (mTabs.empty())
        {
            mActiveTabIndex = -1;
        }
        else
        {
            // 다른 탭의 X 로 닫은 경우 현재 보고 있던 탭을 그대로 유지한다.
            int next = mActiveTabIndex;
            if (index < mActiveTabIndex) next = mActiveTabIndex - 1;
            SwitchToTab(std::clamp(next, 0, static_cast<int>(mTabs.size()) - 1));
        }
        InvalidateRect(mTabControl, nullptr, FALSE);
        InvalidateTerminalArea();
    }

    void MainWindow::SwitchToTab(int index)
    {
        if (index < 0 || index >= static_cast<int>(mTabs.size())) return;
        mActiveTabIndex = index;
        TabCtrl_SetCurSel(mTabControl, index);
        UpdateScrollBar();
        InvalidateTerminalArea();
        SetFocus(mHwnd);
    }

    void MainWindow::SwitchTabRelative(int delta)
    {
        if (mTabs.empty()) return;
        int count = static_cast<int>(mTabs.size());
        int next = ((mActiveTabIndex + delta) % count + count) % count;
        SwitchToTab(next);
    }

    MainWindow::TabState* MainWindow::ActiveTab()
    {
        if (mActiveTabIndex < 0 || mActiveTabIndex >= static_cast<int>(mTabs.size())) return nullptr;
        return mTabs[mActiveTabIndex].get();
    }

    MainWindow::TabState* MainWindow::FindTabById(int tabId)
    {
        for (auto& tab : mTabs) if (tab->id == tabId) return tab.get();
        return nullptr;
    }

    void MainWindow::UpdateTabTitle(int tabId, const std::wstring& title)
    {
        for (size_t i = 0; i < mTabs.size(); ++i)
        {
            if (mTabs[i]->id == tabId)
            {
                mTabs[i]->title = title;
                TCITEMW item{};
                item.mask = TCIF_TEXT;
                item.pszText = const_cast<LPWSTR>(mTabs[i]->title.c_str());
                TabCtrl_SetItem(mTabControl, static_cast<int>(i), &item);
                break;
            }
        }
    }

    void MainWindow::OnConnectionData(int tabId, std::vector<uint8_t>* data)
    {
        std::unique_ptr<std::vector<uint8_t>> owned(data);
        TabState* tab = FindTabById(tabId);
        if (!tab) return;

        int totalBefore = tab->buffer.TotalScrollableLines();
        auto codePoints = tab->decoder.Decode(owned->data(), owned->size());
        tab->parser.Feed(codePoints);

        // 스크롤을 올려 과거 내용을 보는 중이었다면, 새 출력이 쌓여도 보던 위치가
        // 그대로 유지되도록(화면이 아래로 안 밀리도록) 스크롤백이 늘어난 만큼 보정한다.
        if (tab->scrollOffset > 0)
        {
            int grew = tab->buffer.TotalScrollableLines() - totalBefore;
            if (grew > 0) tab->scrollOffset += grew;
        }

        // 스크롤바 갱신은 OnPaint 에서 한 번에 처리한다(출력이 몰릴 때 비-클라이언트
        // 영역이 과도하게 다시 그려져 깜빡이는 것을 방지). 여기서는 다시 그리라고만 요청.
        if (ActiveTab() == tab) InvalidateTerminalArea();
    }

    void MainWindow::OnConnectionClosed(int tabId)
    {
        TabState* tab = FindTabById(tabId);
        if (!tab) return;
        tab->closed = true;
        UpdateTabTitle(tabId, tab->title + L" (닫힘)");
        InvalidateRect(mTabControl, nullptr, FALSE);
    }

    void MainWindow::OnConnectionError(int tabId, std::wstring* message)
    {
        std::unique_ptr<std::wstring> owned(message);
        MessageBoxW(mHwnd, owned->c_str(), L"연결 오류", MB_ICONERROR | MB_OK);
    }

    void MainWindow::OnConnectionReady(int tabId, IConnection* connection)
    {
        TabState* tab = FindTabById(tabId);
        if (!tab)
        {
            delete connection; // 연결 완료 전에 탭이 닫힌 경우
            return;
        }
        tab->connection.reset(connection);
        InvalidateRect(mTabControl, nullptr, FALSE); // 연결 상태 점 갱신
        int cols, rows;
        RecomputeGridSize(&cols, &rows);
        connection->Resize(cols, rows);
    }

    void MainWindow::OnDestroy()
    {
        mTabs.clear();
        PostQuitMessage(0);
    }
}
