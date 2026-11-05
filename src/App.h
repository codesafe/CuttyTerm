#pragma once
#include "Config/AppConfig.h"
#include <Windows.h>

namespace Cutty
{
    // 애플리케이션 진입점 역할: 설정 로드/저장과 메인 윈도우 메시지 루프를 담당한다.
    class App
    {
    public:
        int Run(HINSTANCE instance, int showCommand);

    private:
        AppConfig mConfig;
    };
}
