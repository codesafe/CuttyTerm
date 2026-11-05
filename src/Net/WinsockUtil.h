#pragma once
#include <winsock2.h>
#include <string>

namespace Cutty
{
    // 프로세스 전체에서 WinSock 초기화를 참조 카운트로 관리한다 (탭마다 연결 객체가
    // 생겨도 안전하게 WSAStartup/WSACleanup 짝을 맞추기 위함).
    void AcquireWinsock();
    void ReleaseWinsock();

    // host:port 에 블로킹 TCP 소켓을 연결한다. 실패 시 INVALID_SOCKET 을 반환하고
    // errorMessage 에 사용자에게 보여줄 메시지를 채운다.
    SOCKET ConnectTcpSocket(const std::wstring& host, uint16_t port, std::wstring& errorMessage);
}
