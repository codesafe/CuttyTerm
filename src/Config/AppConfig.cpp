#include "AppConfig.h"
#include "../Common/StringUtil.h"
#include "PasswordProtect.h"
#include <Windows.h>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace Cutty
{
    std::wstring AppConfig::GetAppDirectory()
    {
        // exe 가 있는 폴더를 기준으로 저장한다("옵션/연결 저장을 App이 위치한
        // 폴더에" 요구사항) - %APPDATA% 대신 실행 파일 옆에 두는 휴대용(portable)
        // 방식이라 exe 를 옮기면 설정도 같이 따라간다.
        wchar_t exePath[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::wstring path = exePath;
        size_t lastSlash = path.find_last_of(L"\\/");
        return lastSlash == std::wstring::npos ? L"." : path.substr(0, lastSlash);
    }

    std::wstring AppConfig::GetConfigFilePath()
    {
        return GetAppDirectory() + L"\\config.json";
    }

    bool AppConfig::Load()
    {
        std::ifstream file(GetConfigFilePath());
        if (!file.is_open()) return false;

        json root;
        try
        {
            file >> root;
        }
        catch (...)
        {
            return false;
        }

        fontFamily = FromUtf8(root.value("fontFamily", ToUtf8(fontFamily)));
        fontSize = root.value("fontSize", fontSize);
        foregroundColor = root.value("foregroundColor", foregroundColor);
        backgroundColor = root.value("backgroundColor", backgroundColor);

        connections.clear();
        if (root.contains("connections") && root["connections"].is_array())
        {
            for (const auto& item : root["connections"])
            {
                ConnectionProfile profile;
                profile.name = FromUtf8(item.value("name", std::string()));
                profile.host = FromUtf8(item.value("host", std::string()));
                profile.port = static_cast<uint16_t>(item.value("port", 23));
                profile.protocol = item.value("protocol", std::string("telnet")) == "ssh" ? ProtocolType::Ssh : ProtocolType::Telnet;
                profile.username = FromUtf8(item.value("username", std::string()));
                // 저장된 비밀번호는 DPAPI 로 암호화되어 있으므로 복호화해서 메모리에 올림
                // (다른 Windows 계정에서 만들어졌거나 손상됐으면 조용히 빈 값이 됨).
                profile.password = UnprotectPassword(item.value("passwordProtected", std::string()));
                profile.privateKeyFile = FromUtf8(item.value("privateKeyFile", std::string()));
                profile.codePage = item.value("codePage", 0u);
                connections.push_back(std::move(profile));
            }
        }

        return true;
    }

    bool AppConfig::Save() const
    {
        json root;
        root["fontFamily"] = ToUtf8(fontFamily);
        root["fontSize"] = fontSize;
        root["foregroundColor"] = foregroundColor;
        root["backgroundColor"] = backgroundColor;

        json connArray = json::array();
        for (const auto& profile : connections)
        {
            json item;
            item["name"] = ToUtf8(profile.name);
            item["host"] = ToUtf8(profile.host);
            item["port"] = profile.port;
            item["protocol"] = profile.protocol == ProtocolType::Ssh ? "ssh" : "telnet";
            item["username"] = ToUtf8(profile.username);
            // 비밀번호는 평문으로 저장하지 않고 DPAPI 암호화(현재 Windows 계정 전용) 후 저장.
            item["passwordProtected"] = ProtectPassword(profile.password);
            item["privateKeyFile"] = ToUtf8(profile.privateKeyFile);
            item["codePage"] = profile.codePage;
            connArray.push_back(std::move(item));
        }
        root["connections"] = std::move(connArray);

        std::ofstream file(GetConfigFilePath());
        if (!file.is_open()) return false;
        file << root.dump(2);
        return true;
    }
}
