#pragma once

#include <windows.h>
#include <objbase.h>

#include <string>
#include <vector>
#include <cwchar>
#include <iterator>

namespace AsioSplitter::Drivers
{
inline constexpr wchar_t CONFIG_KEY[] = L"Software\\ASIO-OBS-Splitter";
inline constexpr wchar_t CONFIG_VALUE[] = L"DriverCLSID";
inline constexpr wchar_t ASIO_KEY[] = L"SOFTWARE\\ASIO";
inline const CLSID PROXY_CLSID =
    { 0xA5C8E531, 0x9F22, 0x4D9A, { 0x8C, 0x37, 0xF7, 0x95, 0x26, 0xC8, 0xD8, 0xE1 } };
inline const CLSID ASIO4ALL_CLSID =
    { 0x232685C6, 0x6548, 0x49D8, { 0x84, 0x6D, 0x41, 0x41, 0xA3, 0xEF, 0x75, 0x60 } };

struct DriverInfo
{
    std::wstring name;
    CLSID clsid{};
};

inline bool readStringValue(HKEY key, const wchar_t* valueName, std::wstring& value)
{
    DWORD type = 0;
    DWORD size = 0;
    if (RegQueryValueExW(key, valueName, nullptr, &type, nullptr, &size) != ERROR_SUCCESS ||
        (type != REG_SZ && type != REG_EXPAND_SZ) || size < sizeof(wchar_t))
    {
        return false;
    }

    std::vector<wchar_t> buffer(size / sizeof(wchar_t));
    if (RegQueryValueExW(key, valueName, nullptr, &type,
                         reinterpret_cast<BYTE*>(buffer.data()), &size) != ERROR_SUCCESS)
    {
        return false;
    }

    value.assign(buffer.data());
    return true;
}

inline std::vector<DriverInfo> enumerate()
{
    std::vector<DriverInfo> drivers;
    HKEY asioKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, ASIO_KEY, 0, KEY_READ | KEY_WOW64_64KEY, &asioKey) != ERROR_SUCCESS)
    {
        return drivers;
    }

    for (DWORD index = 0;; ++index)
    {
        wchar_t driverKeyName[256]{};
        DWORD driverKeyNameSize = static_cast<DWORD>(std::size(driverKeyName));
        if (RegEnumKeyExW(asioKey, index, driverKeyName, &driverKeyNameSize,
                          nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
        {
            break;
        }

        HKEY driverKey = nullptr;
        if (RegOpenKeyExW(asioKey, driverKeyName, 0, KEY_READ, &driverKey) != ERROR_SUCCESS)
        {
            continue;
        }

        std::wstring clsidText;
        std::wstring description;
        const bool hasClsid = readStringValue(driverKey, L"CLSID", clsidText);
        const bool hasDescription = readStringValue(driverKey, L"Description", description);
        RegCloseKey(driverKey);

        CLSID clsid{};
        if (!hasClsid || FAILED(CLSIDFromString(clsidText.c_str(), &clsid)))
        {
            continue;
        }

        if (IsEqualCLSID(clsid, PROXY_CLSID) || IsEqualCLSID(clsid, ASIO4ALL_CLSID))
        {
            continue;
        }

        drivers.push_back({hasDescription && !description.empty()
                               ? description
                               : std::wstring(driverKeyName),
                           clsid});
    }

    RegCloseKey(asioKey);
    return drivers;
}

inline bool readSelected(CLSID& clsid)
{
    HKEY configKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, CONFIG_KEY, 0, KEY_READ, &configKey) != ERROR_SUCCESS)
    {
        return false;
    }

    std::wstring clsidText;
    const bool read = readStringValue(configKey, CONFIG_VALUE, clsidText);
    RegCloseKey(configKey);
    return read && SUCCEEDED(CLSIDFromString(clsidText.c_str(), &clsid)) &&
           !IsEqualCLSID(clsid, ASIO4ALL_CLSID);
}

inline bool writeSelected(REFCLSID clsid)
{
    if (IsEqualCLSID(clsid, ASIO4ALL_CLSID) || IsEqualCLSID(clsid, PROXY_CLSID))
    {
        return false;
    }

    HKEY configKey = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, CONFIG_KEY, 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr,
                        &configKey, nullptr) != ERROR_SUCCESS)
    {
        return false;
    }

    LPOLESTR clsidText = nullptr;
    const HRESULT stringResult = StringFromCLSID(clsid, &clsidText);
    const LONG registryResult = SUCCEEDED(stringResult)
        ? RegSetValueExW(configKey, CONFIG_VALUE, 0, REG_SZ,
                         reinterpret_cast<const BYTE*>(clsidText),
                         static_cast<DWORD>((wcslen(clsidText) + 1) * sizeof(wchar_t)))
        : ERROR_INVALID_DATA;

    if (clsidText)
    {
        CoTaskMemFree(clsidText);
    }
    RegCloseKey(configKey);
    return registryResult == ERROR_SUCCESS;
}
}
