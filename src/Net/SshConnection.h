#pragma once
#include "IConnection.h"
#include "WinsockUtil.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>
#include <optional>

struct _LIBSSH2_SESSION;
struct _LIBSSH2_CHANNEL;
struct _LIBSSH2_USERAUTH_KBDINT_PROMPT;
struct _LIBSSH2_USERAUTH_KBDINT_RESPONSE;

namespace Cutty
{
    // libssh2 기반 SSH2 클라이언트. 공개키/비밀번호/키보드 대화형 인증 후 PTY 쉘 채널을 연다.
    // 호스트 키는 TOFU(Trust On First Use) 방식으로 known_hosts 파일에 저장한다.
    class SshConnection : public IConnection
    {
    public:
        SshConnection();
        ~SshConnection() override;

        // password 가 비어 있으면 서버가 요구할 때 OnAuthPrompt 로 물어본다.
        // privateKeyFile 이 있으면 공개키 인증을 먼저 시도한다 (OpenSSH/PEM 형식).
        void SetCredentials(const std::wstring& username, const std::wstring& password,
                            const std::wstring& privateKeyFile = L"");

        // 인증 중 사용자 입력이 필요할 때(서버 질문, 비밀번호, 개인키 암호) 호출된다.
        // Connect() 를 호출한 스레드에서 동기적으로 호출되며, 취소하면 nullopt 를 반환한다.
        std::function<std::optional<std::wstring>(const std::wstring& title, const std::wstring& message, bool echo)> OnAuthPrompt;

        // 처음 보는 호스트 키를 만났을 때 호출된다(putty 처럼 사용자 확인을 받기 위함).
        // Connect() 를 호출한 스레드에서 동기적으로(블로킹) 호출되며, true 를 반환해야
        // 신뢰하고 계속 진행한다. 지정하지 않으면 기본적으로 신뢰(과거 동작)한다.
        std::function<bool(const std::wstring& host, const std::wstring& fingerprint, const std::wstring& keyType)> OnHostKeyPrompt;

        bool Connect(const std::wstring& host, uint16_t port) override;
        void Disconnect() override;
        bool IsConnected() const override { return mChannel != nullptr; }
        void Send(const uint8_t* data, size_t length) override;
        void Resize(int columns, int rows) override;

    private:
        void RecvLoop();
        bool VerifyHostKey(const std::wstring& host, uint16_t port);
        bool Authenticate();
        bool TryPublicKey(const std::string& user);
        std::optional<std::wstring> Prompt(const std::wstring& title, const std::wstring& message, bool echo);
        static void KeyboardInteractiveCallback(const char* name, int nameLength, const char* instruction,
                                                int instructionLength, int promptCount,
                                                const ::_LIBSSH2_USERAUTH_KBDINT_PROMPT* prompts,
                                                ::_LIBSSH2_USERAUTH_KBDINT_RESPONSE* responses, void** abstract);

        SOCKET mSocket = INVALID_SOCKET;
        _LIBSSH2_SESSION* mSession = nullptr;
        _LIBSSH2_CHANNEL* mChannel = nullptr;
        std::thread mThread;
        std::atomic<bool> mRunning { false };
        std::mutex mSessionMutex; // libssh2 세션/채널은 스레드 안전하지 않으므로 모든 호출을 직렬화

        std::wstring mUsername;
        std::wstring mPassword;
        std::wstring mPrivateKeyFile;
        std::wstring mHost;
        bool mAuthCanceled = false;          // 사용자가 인증 입력 창에서 취소함
        bool mStoredPasswordOffered = false; // 저장된 비밀번호를 키보드 대화형 질문에 이미 써봤는지
        int mColumns = 80;
        int mRows = 24;
    };
}
