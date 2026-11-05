#include "App.h"
#include "Window/MainWindow.h"

namespace Cutty
{
    int App::Run(HINSTANCE instance, int showCommand)
    {
        mConfig.Load(); // 파일이 없으면 기본값 사용

        MainWindow window(instance, mConfig);
        if (!window.Create(showCommand)) return -1;

        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0))
        {
            if (!TranslateAccelerator(window.Handle(), window.AcceleratorTable(), &msg))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }

        mConfig.Save();
        return static_cast<int>(msg.wParam);
    }
}
