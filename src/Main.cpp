#include "App.h"

// 애플리케이션 진입점. 실제 로직은 App::Run 에 위임한다.
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow)
{
    Cutty::App app;
    return app.Run(hInstance, nCmdShow);
}
