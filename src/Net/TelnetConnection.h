#pragma once
#include "IConnection.h"
#include "WinsockUtil.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>

namespace Cutty
{
    // RFC854 Telnet 클라이언트. IAC 옵션 협상(ECHO/SGA/TERMINAL-TYPE/NAWS) 최소 구현.
    class TelnetConnection : public IConnection
    {
    public:
        TelnetConnection();
        ~TelnetConnection() override;

        bool Connect(const std::wstring& host, uint16_t port) override;
        void Disconnect() override;
        bool IsConnected() const override { return mSocket != INVALID_SOCKET; }
        void Send(const uint8_t* data, size_t length) override;
        void Resize(int columns, int rows) override;

    private:
        void RecvLoop();
        void SendRaw(const uint8_t* data, size_t length);
        void ProcessIncoming(const uint8_t* data, size_t length);
        void HandleCommand(uint8_t command, uint8_t option);
        void HandleSubnegotiation();
        void SendIac(uint8_t command, uint8_t option);
        void SendNaws();

        enum class ParseState { Data, Iac, Command, Subneg, SubnegIac };

        SOCKET mSocket = INVALID_SOCKET;
        std::thread mRecvThread;
        std::atomic<bool> mRunning { false };
        std::mutex mSendMutex;

        ParseState mParseState = ParseState::Data;
        uint8_t mPendingCommand = 0;
        std::vector<uint8_t> mSubnegBuffer;
        std::vector<uint8_t> mPlainDataAccum; // IAC 제거된 순수 데이터 임시 축적 버퍼

        int mColumns = 80;
        int mRows = 24;
    };
}
