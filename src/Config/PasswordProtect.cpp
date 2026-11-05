#include "PasswordProtect.h"
#include "../Common/StringUtil.h"
#include <Windows.h>
#include <wincrypt.h>
#include <vector>

#pragma comment(lib, "crypt32.lib")

namespace Cutty
{
    std::string ProtectPassword(const std::wstring& plainPassword)
    {
        if (plainPassword.empty()) return {};

        std::string utf8 = ToUtf8(plainPassword);

        DATA_BLOB input{};
        input.pbData = reinterpret_cast<BYTE*>(utf8.data());
        input.cbData = static_cast<DWORD>(utf8.size());

        DATA_BLOB output{};
        // CRYPTPROTECT_UI_FORBIDDEN: 절대 UI 프롬프트를 띄우지 않음(백그라운드 저장이므로).
        if (!CryptProtectData(&input, L"Cutty connection password", nullptr, nullptr, nullptr,
                               CRYPTPROTECT_UI_FORBIDDEN, &output))
        {
            return {};
        }

        DWORD base64Len = 0;
        CryptBinaryToStringA(output.pbData, output.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                              nullptr, &base64Len); // 널 종단 포함 필요 길이
        std::string base64(base64Len, '\0');
        CryptBinaryToStringA(output.pbData, output.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                              base64.data(), &base64Len); // 이번엔 널 제외 실제 길이가 반환됨
        base64.resize(base64Len);

        LocalFree(output.pbData);
        return base64;
    }

    std::wstring UnprotectPassword(const std::string& protectedBase64)
    {
        if (protectedBase64.empty()) return {};

        DWORD binLen = 0;
        if (!CryptStringToBinaryA(protectedBase64.c_str(), 0, CRYPT_STRING_BASE64, nullptr, &binLen, nullptr, nullptr))
        {
            return {};
        }
        std::vector<BYTE> bin(binLen);
        CryptStringToBinaryA(protectedBase64.c_str(), 0, CRYPT_STRING_BASE64, bin.data(), &binLen, nullptr, nullptr);

        DATA_BLOB input{};
        input.pbData = bin.data();
        input.cbData = binLen;

        DATA_BLOB output{};
        if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output))
        {
            return {}; // 다른 계정에서 만들어졌거나 손상된 경우 - 조용히 실패
        }

        std::string utf8(reinterpret_cast<char*>(output.pbData), output.cbData);
        LocalFree(output.pbData);
        return FromUtf8(utf8);
    }
}
