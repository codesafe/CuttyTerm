#include "WinsockUtil.h"
#include "../Common/StringUtil.h"
#include <ws2tcpip.h>
#include <mutex>

#pragma comment(lib, "ws2_32.lib")

namespace Cutty
{
    namespace
    {
        std::mutex gWinsockMutex;
        int gWinsockRefCount = 0;
    }

    void AcquireWinsock()
    {
        std::lock_guard<std::mutex> lock(gWinsockMutex);
        if (gWinsockRefCount == 0)
        {
            WSADATA data;
            WSAStartup(MAKEWORD(2, 2), &data);
        }
        ++gWinsockRefCount;
    }

    void ReleaseWinsock()
    {
        std::lock_guard<std::mutex> lock(gWinsockMutex);
        if (--gWinsockRefCount == 0) WSACleanup();
    }

    SOCKET ConnectTcpSocket(const std::wstring& host, uint16_t port, std::wstring& errorMessage)
    {
        std::string hostUtf8 = ToUtf8(host);

        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        addrinfo* result = nullptr;
        std::string portStr = std::to_string(port);
        if (getaddrinfo(hostUtf8.c_str(), portStr.c_str(), &hints, &result) != 0)
        {
            errorMessage = L"호스트를 찾을 수 없습니다: " + host;
            return INVALID_SOCKET;
        }

        SOCKET sock = INVALID_SOCKET;
        for (addrinfo* p = result; p != nullptr; p = p->ai_next)
        {
            sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
            if (sock == INVALID_SOCKET) continue;
            if (connect(sock, p->ai_addr, static_cast<int>(p->ai_addrlen)) == 0) break;
            closesocket(sock);
            sock = INVALID_SOCKET;
        }
        freeaddrinfo(result);

        if (sock == INVALID_SOCKET)
        {
            errorMessage = L"연결에 실패했습니다: " + host;
        }
        return sock;
    }
}
