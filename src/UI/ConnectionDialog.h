#pragma once
#include "../Config/AppConfig.h"
#include <Windows.h>
#include <optional>

namespace Cutty
{
    // 연결 관리 다이얼로그: 저장된 연결 목록 조회/추가/삭제 및 접속할 프로필 선택.
    // config.connections 는 함수 안에서 직접 갱신되며, 호출자가 이후 config.Save() 해야 한다.
    class ConnectionDialog
    {
    public:
        static std::optional<ConnectionProfile> Show(HWND parent, HINSTANCE instance, AppConfig& config);
    };

    // SSH 접속 시 사용자명/비밀번호를 입력받는 간단한 로그인 다이얼로그.
    class LoginDialog
    {
    public:
        struct Result
        {
            std::wstring username;
            std::wstring password;
        };

        static std::optional<Result> Show(HWND parent, HINSTANCE instance, const std::wstring& defaultUsername);
    };

    // SSH 인증 도중 서버의 질문(키보드 대화형 인증) 또는 개인키 암호를 입력받는다.
    // 연결 스레드(백그라운드)에서 호출해도 된다 - DialogBox 가 자체 메시지 루프를 돌린다.
    class AuthPromptDialog
    {
    public:
        // echo=false 면 입력이 '*' 로 가려진다. 취소하면 nullopt.
        static std::optional<std::wstring> Show(HWND parent, HINSTANCE instance, const std::wstring& title,
                                                const std::wstring& message, bool echo);
    };
}
