#pragma once
#include "ConnectionProfile.h"
#include <string>
#include <vector>
#include <cstdint>

namespace Cutty
{
    // 앱 전체 설정 (폰트/색상/저장된 연결 목록). exe 가 있는 폴더의 config.json 에
    // 저장된다(휴대용 방식 - %APPDATA% 를 쓰지 않음).
    struct AppConfig
    {
        std::wstring fontFamily = L"Consolas";
        float fontSize = 14.0f;
        uint32_t foregroundColor = 0x00D4D4D4;
        uint32_t backgroundColor = 0x001E1E1E;
        std::vector<ConnectionProfile> connections;

        static std::wstring GetAppDirectory(); // exe 가 위치한 폴더 (known_hosts 등도 같이 씀)
        static std::wstring GetConfigFilePath();

        bool Load();       // 파일이 없으면 기본값 유지하고 false 반환
        bool Save() const;
    };
}
