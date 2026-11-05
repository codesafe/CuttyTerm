#pragma once
#include <functional>
#include <string>
#include <cstdint>
#include <cstddef>

namespace Cutty
{
    // 원격 연결(Telnet/SSH) 공통 인터페이스. 수신은 내부 백그라운드 스레드에서 이뤄지며
    // 콜백은 그 스레드 컨텍스트에서 호출된다 (호출자가 UI 스레드로 마샬링해야 함).
    class IConnection
    {
    public:
        virtual ~IConnection() = default;

        virtual bool Connect(const std::wstring& host, uint16_t port) = 0;
        virtual void Disconnect() = 0;
        virtual bool IsConnected() const = 0;
        virtual void Send(const uint8_t* data, size_t length) = 0;
        virtual void Resize(int columns, int rows) {} // NAWS 등에 사용 (기본은 no-op)

        void Send(const std::string& text)
        {
            Send(reinterpret_cast<const uint8_t*>(text.data()), text.size());
        }

        // 콜백: 수신 데이터, 연결 종료, 오류.
        std::function<void(const uint8_t*, size_t)> OnData;
        std::function<void()> OnClosed;
        std::function<void(const std::wstring&)> OnError;
    };
}
