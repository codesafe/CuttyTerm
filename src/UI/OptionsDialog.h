#pragma once
#include "../Config/AppConfig.h"
#include <Windows.h>
#include <functional>

namespace Cutty
{
    // 옵션 다이얼로그. 컨트롤을 바꿀 때마다 onPreview 콜백으로 즉시 미리보기를 적용하고,
    // OK 를 눌러야 config 가 실제로 갱신된다 (Cancel 시 onPreview(원본) 으로 롤백).
    class OptionsDialog
    {
    public:
        static bool Show(HWND parent, HINSTANCE instance, AppConfig& config,
                          const std::function<void(const AppConfig&)>& onPreview);
    };
}
