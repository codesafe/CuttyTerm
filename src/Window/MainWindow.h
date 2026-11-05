#pragma once
#include "../Config/AppConfig.h"
#include "../Render/D2DContext.h"
#include "../Render/TextRenderer.h"
#include "../Terminal/TerminalBuffer.h"
#include "../Terminal/VtParser.h"
#include "../Terminal/Encoding.h"
#include "../Net/IConnection.h"
#include "../UI/Ime.h"
#include <Windows.h>
#include <vector>
#include <memory>
#include <string>

namespace Cutty
{
    // 최상위 윈도우: 툴바 + 탭 목록(SysTabControl32) + Direct2D 로 그려지는 터미널 화면을
    // 한 창 안에서 모두 관리한다. 탭마다 독립된 TerminalBuffer/VtParser/연결을 가진다.
    class MainWindow
    {
    public:
        MainWindow(HINSTANCE instance, AppConfig& config);
        ~MainWindow();

        bool Create(int showCommand = SW_SHOWDEFAULT);
        HWND Handle() const { return mHwnd; }
        HACCEL AcceleratorTable() const { return mAccelTable; }

    private:
        // 백그라운드 연결 스레드 -> UI 스레드로 전달되는 사용자 정의 메시지들.
        static constexpr UINT kMsgConnData = WM_APP + 1;
        static constexpr UINT kMsgConnClosed = WM_APP + 2;
        static constexpr UINT kMsgConnError = WM_APP + 3;
        static constexpr UINT kMsgConnReady = WM_APP + 4;

        struct TabState
        {
            explicit TabState(int idValue) : id(idValue), parser(buffer) {}

            int id;
            std::wstring title;
            TerminalBuffer buffer;
            VtParser parser; // buffer 를 참조하므로 buffer 다음에 선언되어야 함
            StreamDecoder decoder;
            std::unique_ptr<IConnection> connection;
            ConnectionProfile profile;
            bool closed = false;              // 원격에서 연결이 끊겼는지 (탭의 상태 점 표시용)
            int scrollOffset = 0;             // 스크롤백에서 맨 아래로부터 몇 줄 위를 보고 있는지 (0 = 맨 아래)
            int horizontalScrollOffset = 0;   // 왼쪽에서 몇 칸을 가로로 스크롤했는지 (0 = 맨 왼쪽)

            // 마우스 드래그 선택 (줄은 TerminalBuffer 줄 ID, 컬럼은 가로 스크롤 무관한 절대값).
            bool hasSelection = false;
            int64_t selectionAnchorLine = 0;
            int selectionAnchorColumn = 0;
            int64_t selectionEndLine = 0;
            int selectionEndColumn = 0;
        };

        static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
        LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

        // 터미널 렌더링 전용 자식 윈도우 (D2D 스왑체인을 여기에 바인딩해 툴바/탭
        // 컨트롤이 항상 그 위에 정상적으로 겹쳐 그려지도록 분리한다).
        static LRESULT CALLBACK RenderAreaStaticProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
        LRESULT RenderAreaProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
        void CreateRenderArea();
        void OnRenderAreaSize(int width, int height);

        void OnCreate();
        void OnSize(int width, int height);
        void OnPaint();
        bool OnKeyDown(WPARAM key, bool alt); // 특수키 처리, 처리했으면 true (WM_SYSKEYDOWN 에서도 사용)
        void OnChar(wchar_t ch, bool alt);    // alt=true 면 ESC 접두 (Meta 키)
        void OnMouseWheel(int wheelDelta);
        void OnCommand(WPARAM wParam);
        void OnNotify(LPARAM lParam);
        void OnDestroy();
        void OnConnectionData(int tabId, std::vector<uint8_t>* data);
        void OnConnectionClosed(int tabId);
        void OnConnectionError(int tabId, std::wstring* message);
        void OnConnectionReady(int tabId, IConnection* connection);

        void CreateToolbar();
        void CreateTabControl();
        static LRESULT CALLBACK TabControlSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                                       UINT_PTR id, DWORD_PTR refData);
        void PaintTabStrip(HDC dc, const RECT& client); // 둥근 탭 줄 전체를 직접 그림
        RECT TabBodyRect(int index) const;
        RECT TabCloseButtonRect(int index) const;
        bool HitTabCloseButton(int index, POINT pt) const;
        void LayoutChildren();

        void OpenNewConnection();
        void StartConnection(const ConnectionProfile& profile);
        TabState* AddTab(const ConnectionProfile& profile);
        void RequestCloseTab(int index); // 사용자에게 닫기 확인을 받은 뒤 CloseTab
        void CloseTab(int index);
        void SwitchToTab(int index);
        void SwitchTabRelative(int delta);
        TabState* ActiveTab();
        TabState* FindTabById(int tabId);
        void UpdateTabTitle(int tabId, const std::wstring& title);

        void ApplyPreviewConfig(const AppConfig& preview);
        void ResizeAllTabBuffers();
        void RecomputeGridSize(int* outCols, int* outRows) const;
        void InvalidateTerminalArea();
        RECT TerminalAreaRect() const;

        void ShowOptions();
        void SendKeystrokeBytes(const std::string& bytes);
        void SendCursorKey(char finalChar); // 방향키/Home/End: 'A'~'D', 'H', 'F'
        void ScrollViewByPage(int direction); // Shift+PgUp(+1)/PgDn(-1): 스크롤백 한 화면
        void UpdateScrollBar(); // 활성 탭의 스크롤 범위/위치를 세로+가로 스크롤바에 반영

        // ---- 마우스 선택 / 클립보드 ----
        void HitTestCell(TabState& tab, int x, int y, int64_t* outLine, int* outColumn) const;
        void OnSelectionMouseDown(int x, int y);
        void OnSelectionMouseMove(int x, int y);
        void OnSelectionMouseUp();
        void OnSelectionAutoScroll(); // 드래그 중 마우스가 창 밖에 있으면 계속 스크롤
        static TextSelection NormalizedSelection(const TabState& tab);
        static std::wstring SelectedText(const TabState& tab);
        bool CopySelectionToClipboard();
        void PasteFromClipboard();

        HINSTANCE mInstance;
        HWND mHwnd = nullptr;
        HWND mToolbar = nullptr;
        HWND mTabControl = nullptr;
        HWND mRenderArea = nullptr;
        HACCEL mAccelTable = nullptr;

        AppConfig& mConfig;
        D2DContext mD2DContext;
        TextRenderer mTextRenderer;
        uint32_t mLiveBackground = 0x001E1E1E;
        uint32_t mLiveForeground = 0x00D4D4D4; // 배경/글자색 변경 시 기존 셀 재염색(RecolorDefaults)에 필요

        std::vector<std::unique_ptr<TabState>> mTabs;
        int mActiveTabIndex = -1;
        int mNextTabId = 1;

        ImeComposition mIme;
        bool mSuppressNextChar = false;
        wchar_t mPendingHighSurrogate = 0;

        static constexpr UINT_PTR kSelectionScrollTimer = 1;
        bool mSelecting = false; // 왼쪽 버튼을 누른 채 드래그 중

        int mToolbarHeight = 28;
        int mTabHeight = 32;
        static constexpr int kTabWidth = 170;
        HFONT mTabFont = nullptr;
        int mHoverTabIndex = -1;
        bool mHoverOverClose = false; // 호버 중인 탭의 X 버튼 위에 마우스가 있는지
        ULONG_PTR mGdiplusToken = 0;
    };
}
