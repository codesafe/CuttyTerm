#include "SshConnection.h"
#include "../Common/StringUtil.h"
#include "../Config/AppConfig.h"
#include <libssh2.h>
#include <Windows.h>
#include <string>

#pragma comment(lib, "libssh2.lib")
#pragma comment(lib, "libssl.lib")
#pragma comment(lib, "libcrypto.lib")
#pragma comment(lib, "z.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "ws2_32.lib")

namespace Cutty
{
    namespace
    {
        std::wstring GetKnownHostsPath()
        {
            // config.json 과 같은 폴더(exe 옆)에 둔다 - 휴대용 방식 일관성 유지.
            return AppConfig::GetAppDirectory() + L"\\known_hosts";
        }

        std::wstring HostKeyTypeName(int keyType)
        {
            switch (keyType)
            {
            case LIBSSH2_HOSTKEY_TYPE_RSA: return L"RSA";
            case LIBSSH2_HOSTKEY_TYPE_DSS: return L"DSA";
#if defined(LIBSSH2_HOSTKEY_TYPE_ECDSA_256)
            case LIBSSH2_HOSTKEY_TYPE_ECDSA_256: return L"ECDSA-256";
            case LIBSSH2_HOSTKEY_TYPE_ECDSA_384: return L"ECDSA-384";
            case LIBSSH2_HOSTKEY_TYPE_ECDSA_521: return L"ECDSA-521";
#endif
#if defined(LIBSSH2_HOSTKEY_TYPE_ED25519)
            case LIBSSH2_HOSTKEY_TYPE_ED25519: return L"ED25519";
#endif
            default: return L"UNKNOWN";
            }
        }

        // OpenSSH/PuTTY 가 보여주는 것과 비슷한 형태의 SHA256 지문 문자열을 만든다.
        std::wstring FormatFingerprint(LIBSSH2_SESSION* session)
        {
            const char* sha256 = libssh2_hostkey_hash(session, LIBSSH2_HOSTKEY_HASH_SHA256);
            if (!sha256) return L"(지문을 계산할 수 없음)";

            static const wchar_t* hex = L"0123456789abcdef";
            std::wstring out = L"SHA256:";
            for (int i = 0; i < 32; ++i)
            {
                uint8_t b = static_cast<uint8_t>(sha256[i]);
                out.push_back(hex[b >> 4]);
                out.push_back(hex[b & 0xF]);
            }
            return out;
        }

        int KnownHostKeyBit(int hostkeyType)
        {
            switch (hostkeyType)
            {
            case LIBSSH2_HOSTKEY_TYPE_RSA: return LIBSSH2_KNOWNHOST_KEY_SSHRSA;
            case LIBSSH2_HOSTKEY_TYPE_DSS: return LIBSSH2_KNOWNHOST_KEY_SSHDSS;
#if defined(LIBSSH2_HOSTKEY_TYPE_ECDSA_256)
            case LIBSSH2_HOSTKEY_TYPE_ECDSA_256: return LIBSSH2_KNOWNHOST_KEY_ECDSA_256;
            case LIBSSH2_HOSTKEY_TYPE_ECDSA_384: return LIBSSH2_KNOWNHOST_KEY_ECDSA_384;
            case LIBSSH2_HOSTKEY_TYPE_ECDSA_521: return LIBSSH2_KNOWNHOST_KEY_ECDSA_521;
#endif
#if defined(LIBSSH2_HOSTKEY_TYPE_ED25519)
            case LIBSSH2_HOSTKEY_TYPE_ED25519: return LIBSSH2_KNOWNHOST_KEY_ED25519;
#endif
            default: return 0;
            }
        }
    }

    SshConnection::SshConnection()
    {
        AcquireWinsock();
        libssh2_init(0);
    }

    SshConnection::~SshConnection()
    {
        Disconnect();
        libssh2_exit();
        ReleaseWinsock();
    }

    void SshConnection::SetCredentials(const std::wstring& username, const std::wstring& password,
                                       const std::wstring& privateKeyFile)
    {
        mUsername = username;
        mPassword = password;
        mPrivateKeyFile = privateKeyFile;
    }

    std::optional<std::wstring> SshConnection::Prompt(const std::wstring& title, const std::wstring& message, bool echo)
    {
        if (mAuthCanceled || !OnAuthPrompt) return std::nullopt;
        auto answer = OnAuthPrompt(title, message, echo);
        if (!answer) mAuthCanceled = true;
        return answer;
    }

    void SshConnection::KeyboardInteractiveCallback(const char* name, int nameLength, const char* instruction,
                                                    int instructionLength, int promptCount,
                                                    const LIBSSH2_USERAUTH_KBDINT_PROMPT* prompts,
                                                    LIBSSH2_USERAUTH_KBDINT_RESPONSE* responses, void** abstract)
    {
        auto self = static_cast<SshConnection*>(*abstract);
        std::wstring title = nameLength > 0 ? FromUtf8(std::string(name, nameLength)) : L"SSH 인증";
        std::wstring header = instructionLength > 0 ? FromUtf8(std::string(instruction, instructionLength)) + L"\n" : L"";

        for (int i = 0; i < promptCount; ++i)
        {
            std::wstring question = FromUtf8(std::string(reinterpret_cast<const char*>(prompts[i].text), prompts[i].length));
            std::optional<std::wstring> answer;

            // 대부분의 서버(PAM)는 "Password:" 질문 하나만 한다 - 저장된 비밀번호가 있으면
            // 처음 한 번은 그것으로 답하고, 틀렸거나 다른 질문(OTP 등)이면 사용자에게 묻는다.
            if (!prompts[i].echo && !self->mPassword.empty() && !self->mStoredPasswordOffered)
            {
                answer = self->mPassword;
                self->mStoredPasswordOffered = true;
            }
            else
            {
                answer = self->Prompt(title, self->mUsername + L"@" + self->mHost + L"\n" + header + question,
                                      prompts[i].echo != 0);
            }

            // 응답 메모리는 libssh2 가 세션의 free 함수로 해제한다 (세션 생성 시 malloc/free 지정).
            std::string utf8 = answer ? ToUtf8(*answer) : std::string();
            responses[i].text = static_cast<char*>(malloc(utf8.size() + 1));
            if (responses[i].text) memcpy(responses[i].text, utf8.c_str(), utf8.size() + 1);
            responses[i].length = responses[i].text ? static_cast<unsigned int>(utf8.size()) : 0;
            SecureZeroMemory(utf8.data(), utf8.size());
        }
    }

    bool SshConnection::TryPublicKey(const std::string& user)
    {
        // 경로에 한글이 있어도 되도록 파일은 직접 읽어 메모리로 넘긴다.
        std::string keyData;
        FILE* file = nullptr;
        if (_wfopen_s(&file, mPrivateKeyFile.c_str(), L"rb") == 0 && file)
        {
            char buffer[4096];
            size_t n;
            while ((n = fread(buffer, 1, sizeof(buffer), file)) > 0) keyData.append(buffer, n);
            fclose(file);
        }
        if (keyData.empty())
        {
            if (OnError) OnError(L"개인키 파일을 읽을 수 없습니다: " + mPrivateKeyFile);
            return false;
        }
        if (keyData.rfind("PuTTY-User-Key-File", 0) == 0)
        {
            if (OnError) OnError(L"PuTTY .ppk 형식의 키는 지원하지 않습니다. PuTTYgen 의 "
                                 L"'Conversions > Export OpenSSH key' 로 변환한 파일을 지정하세요.");
            return false;
        }

        std::string passphrase;
        bool ok = false;
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            int rc = libssh2_userauth_publickey_frommemory(mSession, user.c_str(), user.size(), nullptr, 0,
                                                           keyData.c_str(), keyData.size(), passphrase.c_str());
            if (rc == 0) { ok = true; break; }
            // 암호가 걸린 키(또는 암호가 틀림)면 파일 해석 단계에서 실패한다 -> 암호를 물어본다.
            // 서버가 키를 거부한 경우(AUTHENTICATION_FAILED 등)는 다음 인증 방법으로 넘어간다.
            if (rc != LIBSSH2_ERROR_FILE || attempt == 3) break;
            auto answer = Prompt(L"개인키 암호",
                                 (attempt == 0 ? L"개인키 암호를 입력하세요." : L"암호가 틀렸거나 지원하지 않는 키 형식입니다. 다시 입력하세요.")
                                 + std::wstring(L"\n") + mPrivateKeyFile, false);
            if (!answer) break;
            SecureZeroMemory(passphrase.data(), passphrase.size());
            passphrase = ToUtf8(*answer);
        }
        SecureZeroMemory(passphrase.data(), passphrase.size());
        SecureZeroMemory(keyData.data(), keyData.size());
        return ok;
    }

    bool SshConnection::Authenticate()
    {
        std::string user = ToUtf8(mUsername);
        const unsigned int userLength = static_cast<unsigned int>(user.size());

        // 서버가 허용하는 인증 방법 목록 (예: "publickey,password,keyboard-interactive").
        const char* list = libssh2_userauth_list(mSession, user.c_str(), userLength);
        if (!list) return libssh2_userauth_authenticated(mSession) != 0; // "none" 인증으로 이미 통과한 경우
        std::string methods = list;
        auto allows = [&](const char* method) { return ("," + methods + ",").find("," + std::string(method) + ",") != std::string::npos; };

        // 1) 공개키
        if (!mPrivateKeyFile.empty() && allows("publickey"))
        {
            if (TryPublicKey(user)) return true;
            if (mAuthCanceled) return false;
        }

        // 2) 저장된 비밀번호
        if (!mPassword.empty() && allows("password"))
        {
            std::string pass = ToUtf8(mPassword);
            int rc = libssh2_userauth_password_ex(mSession, user.c_str(), userLength, pass.c_str(),
                                                  static_cast<unsigned int>(pass.size()), nullptr);
            SecureZeroMemory(pass.data(), pass.size());
            if (rc == 0) return true;
        }

        // 3) 키보드 대화형 (PAM 서버, OTP 등) - 서버의 질문을 창으로 띄워 답한다.
        if (allows("keyboard-interactive"))
        {
            for (int attempt = 0; attempt < 3 && !mAuthCanceled; ++attempt)
            {
                if (libssh2_userauth_keyboard_interactive_ex(mSession, user.c_str(), userLength,
                                                             &SshConnection::KeyboardInteractiveCallback) == 0)
                {
                    return true;
                }
            }
            if (mAuthCanceled) return false;
        }

        // 4) 비밀번호를 물어보고 시도
        if (allows("password"))
        {
            for (int attempt = 0; attempt < 3; ++attempt)
            {
                auto answer = Prompt(L"SSH 비밀번호",
                                     (attempt == 0 ? L"" : L"비밀번호가 틀렸습니다.\n") + mUsername + L"@" + mHost + L" 의 비밀번호:",
                                     false);
                if (!answer) return false;
                std::string pass = ToUtf8(*answer);
                int rc = libssh2_userauth_password_ex(mSession, user.c_str(), userLength, pass.c_str(),
                                                      static_cast<unsigned int>(pass.size()), nullptr);
                SecureZeroMemory(pass.data(), pass.size());
                if (rc == 0) return true;
            }
        }

        if (OnError) OnError(L"인증에 실패했습니다. (서버가 허용하는 방법: " + FromUtf8(methods) + L")");
        return false;
    }

    bool SshConnection::VerifyHostKey(const std::wstring& host, uint16_t port)
    {
        size_t keyLen = 0;
        int keyType = 0;
        const char* fingerprint = libssh2_session_hostkey(mSession, &keyLen, &keyType);
        if (!fingerprint)
        {
            if (OnError) OnError(L"호스트 키를 가져올 수 없습니다.");
            return false;
        }

        LIBSSH2_KNOWNHOSTS* knownHosts = libssh2_knownhost_init(mSession);
        if (!knownHosts) return true; // 매우 드문 실패: 검증 없이 진행

        std::string knownHostsPath = ToUtf8(GetKnownHostsPath());
        libssh2_knownhost_readfile(knownHosts, knownHostsPath.c_str(), LIBSSH2_KNOWNHOST_FILE_OPENSSH);

        int mask = LIBSSH2_KNOWNHOST_TYPE_PLAIN | LIBSSH2_KNOWNHOST_KEYENC_RAW | KnownHostKeyBit(keyType);
        std::string hostUtf8 = ToUtf8(host);
        libssh2_knownhost* match = nullptr;
        int check = libssh2_knownhost_checkp(knownHosts, hostUtf8.c_str(), port, fingerprint, keyLen, mask, &match);

        bool ok = false;
        switch (check)
        {
        case LIBSSH2_KNOWNHOST_CHECK_MATCH:
            ok = true;
            break;
        case LIBSSH2_KNOWNHOST_CHECK_NOTFOUND:
        {
            // 최초 접속(TOFU): putty 처럼 사용자에게 신뢰 여부를 먼저 물어본다.
            std::wstring fingerprintHex = FormatFingerprint(mSession);
            std::wstring keyTypeName = HostKeyTypeName(keyType);
            // OpenSSH/PuTTY 처럼 22 번이 아닌 포트는 "[host]:port" 로 따로 저장한다
            // (같은 주소의 다른 포트 서버가 확인 없이 통과하지 않도록).
            std::wstring displayHost = port == 22 ? host : host + L":" + std::to_wstring(port);
            std::string entryName = port == 22 ? hostUtf8 : "[" + hostUtf8 + "]:" + std::to_string(port);
            bool trust = OnHostKeyPrompt ? OnHostKeyPrompt(displayHost, fingerprintHex, keyTypeName) : true;
            if (trust)
            {
                libssh2_knownhost_addc(knownHosts, entryName.c_str(), "", fingerprint, keyLen, nullptr, 0, mask, nullptr);
                libssh2_knownhost_writefile(knownHosts, knownHostsPath.c_str(), LIBSSH2_KNOWNHOST_FILE_OPENSSH);
                ok = true;
            }
            else
            {
                if (OnError) OnError(L"사용자가 호스트 키를 신뢰하지 않아 연결을 취소했습니다.");
                ok = false;
            }
            break;
        }
        case LIBSSH2_KNOWNHOST_CHECK_MISMATCH:
            if (OnError) OnError(L"경고: 호스트 키가 이전과 다릅니다. 중간자 공격(MITM) 가능성이 있어 연결을 중단합니다.");
            ok = false;
            break;
        default:
            if (OnError) OnError(L"호스트 키 검증에 실패했습니다.");
            ok = false;
            break;
        }

        libssh2_knownhost_free(knownHosts);
        return ok;
    }

    bool SshConnection::Connect(const std::wstring& host, uint16_t port)
    {
        Disconnect();

        std::wstring error;
        SOCKET sock = ConnectTcpSocket(host, port, error);
        if (sock == INVALID_SOCKET)
        {
            if (OnError) OnError(error);
            return false;
        }

        // abstract 에 this 를 넣어 키보드 대화형 콜백에서 꺼내 쓴다. 할당 함수도 직접 지정해
        // 콜백에서 malloc 한 응답을 libssh2 가 같은 CRT 의 free 로 해제하도록 한다.
        LIBSSH2_SESSION* session = libssh2_session_init_ex(
            [](size_t count, void**) -> void* { return malloc(count); },
            [](void* ptr, void**) { free(ptr); },
            [](void* ptr, size_t count, void**) -> void* { return realloc(ptr, count); },
            this);
        if (!session)
        {
            closesocket(sock);
            if (OnError) OnError(L"SSH 세션을 생성할 수 없습니다.");
            return false;
        }

        libssh2_session_set_blocking(session, 1); // 핸드셰이크/인증은 블로킹으로 단순하게 처리
        if (libssh2_session_handshake(session, sock) != 0)
        {
            libssh2_session_free(session);
            closesocket(sock);
            if (OnError) OnError(L"SSH 핸드셰이크에 실패했습니다.");
            return false;
        }

        mSocket = sock;
        mSession = session;

        if (!VerifyHostKey(host, port))
        {
            Disconnect();
            return false;
        }

        mHost = host;
        mAuthCanceled = false;
        mStoredPasswordOffered = false;
        if (!Authenticate())
        {
            if (mAuthCanceled && OnError) OnError(L"인증을 취소했습니다.");
            Disconnect();
            return false;
        }

        LIBSSH2_CHANNEL* channel = libssh2_channel_open_session(mSession);
        if (!channel)
        {
            if (OnError) OnError(L"채널을 열 수 없습니다.");
            Disconnect();
            return false;
        }

        libssh2_channel_request_pty_ex(channel, "xterm-256color", 14, nullptr, 0, mColumns, mRows, 0, 0);
        if (libssh2_channel_shell(channel) != 0)
        {
            libssh2_channel_free(channel);
            if (OnError) OnError(L"쉘을 시작할 수 없습니다.");
            Disconnect();
            return false;
        }

        mChannel = channel;

        // 이후 입출력은 논블로킹으로 전환해 recv 스레드가 짧게 폴링하도록 한다.
        libssh2_session_set_blocking(mSession, 0);

        mRunning = true;
        mThread = std::thread(&SshConnection::RecvLoop, this);
        return true;
    }

    void SshConnection::Disconnect()
    {
        mRunning = false;
        if (mThread.joinable()) mThread.join();

        std::lock_guard<std::mutex> lock(mSessionMutex);
        if (mChannel)
        {
            libssh2_channel_free(mChannel);
            mChannel = nullptr;
        }
        if (mSession)
        {
            libssh2_session_disconnect(mSession, "bye");
            libssh2_session_free(mSession);
            mSession = nullptr;
        }
        if (mSocket != INVALID_SOCKET)
        {
            closesocket(mSocket);
            mSocket = INVALID_SOCKET;
        }
    }

    void SshConnection::Send(const uint8_t* data, size_t length)
    {
        size_t sent = 0;
        while (sent < length)
        {
            std::lock_guard<std::mutex> lock(mSessionMutex);
            if (!mChannel) return;
            ssize_t n = libssh2_channel_write(mChannel, reinterpret_cast<const char*>(data + sent), length - sent);
            if (n == LIBSSH2_ERROR_EAGAIN)
            {
                Sleep(1);
                continue;
            }
            if (n < 0) return;
            sent += static_cast<size_t>(n);
        }
    }

    void SshConnection::Resize(int columns, int rows)
    {
        mColumns = columns;
        mRows = rows;
        std::lock_guard<std::mutex> lock(mSessionMutex);
        if (mChannel) libssh2_channel_request_pty_size_ex(mChannel, columns, rows, 0, 0);
    }

    void SshConnection::RecvLoop()
    {
        char buffer[4096];
        while (mRunning)
        {
            ssize_t n;
            {
                std::lock_guard<std::mutex> lock(mSessionMutex);
                if (!mChannel) break;
                n = libssh2_channel_read(mChannel, buffer, sizeof(buffer));
            }

            if (n == LIBSSH2_ERROR_EAGAIN)
            {
                Sleep(15);
                continue;
            }
            if (n < 0) break;
            if (n > 0 && OnData) OnData(reinterpret_cast<uint8_t*>(buffer), static_cast<size_t>(n));

            bool eof;
            {
                std::lock_guard<std::mutex> lock(mSessionMutex);
                eof = mChannel && libssh2_channel_eof(mChannel) != 0;
            }
            if (eof) break;
        }

        mRunning = false;
        if (OnClosed) OnClosed();
    }
}
