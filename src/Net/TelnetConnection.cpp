#include "TelnetConnection.h"
#include <string>

namespace Cutty
{
    namespace
    {
        constexpr uint8_t kIac = 255;
        constexpr uint8_t kDont = 254;
        constexpr uint8_t kDo = 253;
        constexpr uint8_t kWont = 252;
        constexpr uint8_t kWill = 251;
        constexpr uint8_t kSb = 250;
        constexpr uint8_t kSe = 240;

        constexpr uint8_t kOptEcho = 1;
        constexpr uint8_t kOptSga = 3;
        constexpr uint8_t kOptTType = 24;
        constexpr uint8_t kOptNaws = 31;
    }

    TelnetConnection::TelnetConnection()
    {
        AcquireWinsock();
    }

    TelnetConnection::~TelnetConnection()
    {
        Disconnect();
        ReleaseWinsock();
    }

    bool TelnetConnection::Connect(const std::wstring& host, uint16_t port)
    {
        Disconnect();

        std::wstring error;
        SOCKET sock = ConnectTcpSocket(host, port, error);
        if (sock == INVALID_SOCKET)
        {
            if (OnError) OnError(error);
            return false;
        }

        mSocket = sock;
        mRunning = true;
        mRecvThread = std::thread(&TelnetConnection::RecvLoop, this);
        return true;
    }

    void TelnetConnection::Disconnect()
    {
        mRunning = false;
        if (mSocket != INVALID_SOCKET)
        {
            shutdown(mSocket, SD_BOTH);
            closesocket(mSocket);
            mSocket = INVALID_SOCKET;
        }
        if (mRecvThread.joinable()) mRecvThread.join();
    }

    void TelnetConnection::SendRaw(const uint8_t* data, size_t length)
    {
        if (mSocket == INVALID_SOCKET) return;
        std::lock_guard<std::mutex> lock(mSendMutex);
        size_t sent = 0;
        while (sent < length)
        {
            int n = send(mSocket, reinterpret_cast<const char*>(data + sent), static_cast<int>(length - sent), 0);
            if (n <= 0) break;
            sent += static_cast<size_t>(n);
        }
    }

    void TelnetConnection::Send(const uint8_t* data, size_t length)
    {
        // 사용자가 입력한 순수 데이터 중 0xFF(IAC) 바이트는 이스케이프(중복 전송) 해야 한다.
        std::vector<uint8_t> escaped;
        escaped.reserve(length);
        for (size_t i = 0; i < length; ++i)
        {
            escaped.push_back(data[i]);
            if (data[i] == kIac) escaped.push_back(kIac);
        }
        SendRaw(escaped.data(), escaped.size());
    }

    void TelnetConnection::SendIac(uint8_t command, uint8_t option)
    {
        uint8_t buf[3] = { kIac, command, option };
        SendRaw(buf, 3);
    }

    void TelnetConnection::SendNaws()
    {
        uint8_t buf[9] = { kIac, kSb, kOptNaws,
            static_cast<uint8_t>(mColumns >> 8), static_cast<uint8_t>(mColumns & 0xFF),
            static_cast<uint8_t>(mRows >> 8), static_cast<uint8_t>(mRows & 0xFF),
            kIac, kSe };
        SendRaw(buf, 9);
    }

    void TelnetConnection::Resize(int columns, int rows)
    {
        mColumns = columns;
        mRows = rows;
        if (IsConnected()) SendNaws();
    }

    void TelnetConnection::HandleCommand(uint8_t command, uint8_t option)
    {
        switch (command)
        {
        case kWill:
            if (option == kOptEcho || option == kOptSga) SendIac(kDo, option);
            else SendIac(kDont, option);
            break;
        case kWont:
            SendIac(kDont, option);
            break;
        case kDo:
            if (option == kOptTType || option == kOptNaws)
            {
                SendIac(kWill, option);
                if (option == kOptNaws) SendNaws();
            }
            else
            {
                SendIac(kWont, option);
            }
            break;
        case kDont:
            SendIac(kWont, option);
            break;
        default:
            break;
        }
    }

    void TelnetConnection::HandleSubnegotiation()
    {
        if (mSubnegBuffer.empty()) return;

        if (mSubnegBuffer[0] == kOptTType && mSubnegBuffer.size() >= 2 && mSubnegBuffer[1] == 1 /*SEND*/)
        {
            static const char termType[] = "xterm-256color";
            std::vector<uint8_t> reply;
            reply.push_back(kIac);
            reply.push_back(kSb);
            reply.push_back(kOptTType);
            reply.push_back(0); // IS
            reply.insert(reply.end(), termType, termType + sizeof(termType) - 1);
            reply.push_back(kIac);
            reply.push_back(kSe);
            SendRaw(reply.data(), reply.size());
        }
    }

    void TelnetConnection::ProcessIncoming(const uint8_t* data, size_t length)
    {
        mPlainDataAccum.clear();

        for (size_t i = 0; i < length; ++i)
        {
            uint8_t b = data[i];
            switch (mParseState)
            {
            case ParseState::Data:
                if (b == kIac) mParseState = ParseState::Iac;
                else mPlainDataAccum.push_back(b);
                break;

            case ParseState::Iac:
                if (b == kIac) { mPlainDataAccum.push_back(b); mParseState = ParseState::Data; } // 이스케이프된 0xFF
                else if (b == kWill || b == kWont || b == kDo || b == kDont) { mPendingCommand = b; mParseState = ParseState::Command; }
                else if (b == kSb) { mSubnegBuffer.clear(); mParseState = ParseState::Subneg; }
                else mParseState = ParseState::Data; // GA 등 옵션 없는 명령은 무시
                break;

            case ParseState::Command:
                HandleCommand(mPendingCommand, b);
                mParseState = ParseState::Data;
                break;

            case ParseState::Subneg:
                if (b == kIac) mParseState = ParseState::SubnegIac;
                else mSubnegBuffer.push_back(b);
                break;

            case ParseState::SubnegIac:
                if (b == kSe) { HandleSubnegotiation(); mParseState = ParseState::Data; }
                else { mSubnegBuffer.push_back(b); mParseState = ParseState::Subneg; } // 드문 케이스
                break;
            }
        }

        if (!mPlainDataAccum.empty() && OnData) OnData(mPlainDataAccum.data(), mPlainDataAccum.size());
    }

    void TelnetConnection::RecvLoop()
    {
        uint8_t buffer[4096];
        while (mRunning)
        {
            int n = recv(mSocket, reinterpret_cast<char*>(buffer), sizeof(buffer), 0);
            if (n <= 0) break;
            ProcessIncoming(buffer, static_cast<size_t>(n));
        }

        if (mSocket != INVALID_SOCKET)
        {
            closesocket(mSocket);
            mSocket = INVALID_SOCKET;
        }
        if (OnClosed) OnClosed();
    }
}
