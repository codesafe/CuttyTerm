#pragma once
#include <string>

namespace Cutty
{
    // Windows DPAPI(CryptProtectData) 로 비밀번호를 암호화해 파일에 저장 가능한
    // base64 문자열로 바꾼다. 복호화는 같은 Windows 사용자 계정에서만 가능하므로,
    // config.json 파일 자체가 유출되어도 다른 사람/다른 계정에서는 읽을 수 없다.
    std::string ProtectPassword(const std::wstring& plainPassword);
    std::wstring UnprotectPassword(const std::string& protectedBase64);
}
