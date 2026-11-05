#pragma once
#include <string>

namespace Cutty
{
    enum class ProtocolType
    {
        Telnet,
        Ssh,
    };

    // 사용자가 저장한 연결 정보 하나.
    struct ConnectionProfile
    {
        std::wstring name;      // 목록에 표시될 이름
        std::wstring host;
        uint16_t port = 23;
        ProtocolType protocol = ProtocolType::Telnet;
        std::wstring username;  // SSH 용
        std::wstring password;  // 메모리상 평문(복호화된 값). 파일 저장 시에는
                                 // AppConfig 가 DPAPI 로 암호화해서 기록한다.
                                 // 비어있으면 접속 중 서버가 요구할 때 입력받는다.
        std::wstring privateKeyFile; // SSH 공개키 인증용 개인키 파일(OpenSSH/PEM 형식). 비면 사용 안 함
        unsigned codePage = 0;       // 0 = 자동 감지, 그 외 Windows 코드페이지 (65001, 949 ...)
    };
}
