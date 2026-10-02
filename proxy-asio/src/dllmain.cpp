#include <windows.h>
#include <objbase.h>
#include "proxy_asio.h"

static const CLSID CLSID_ProxyAsio = 
    { 0xa5c8e531, 0x9f22, 0x4d9a, { 0x8c, 0x37, 0xf7, 0x95, 0x26, 0xc8, 0xd8, 0xe1 } };

static const char* DRIVER_NAME = "Proxy ASIO (OBS Splitter)";
static const char* DRIVER_DESC = "Low Latency ASIO Proxy Splitter for OBS";

static HINSTANCE g_hModule = nullptr;
static LONG g_serverLocks = 0;

// -----------------------------------------------------------------------------
// IClassFactory 구현체
// -----------------------------------------------------------------------------
class ProxyClassFactory : public IClassFactory
{
public:
    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override
    {
        if (!ppvObject) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IClassFactory)
        {
            *ppvObject = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }

    virtual ULONG STDMETHODCALLTYPE AddRef() override
    {
        return 2; // 스택/정적 객체이므로 카운트 유지
    }

    virtual ULONG STDMETHODCALLTYPE Release() override
    {
        return 1;
    }

    virtual HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppvObject) override
    {
        OutputDebugStringA("[ASIO_PROXY] CreateInstance called\n");

        if (!ppvObject) return E_POINTER;
        *ppvObject = nullptr;

        if (pUnkOuter != nullptr)
        {
            return CLASS_E_NOAGGREGATION;
        }

        ProxyAsio* pDriver = new ProxyAsio();
        if (!pDriver)
        {
            return E_OUTOFMEMORY;
        }

        HRESULT hr = pDriver->QueryInterface(riid, ppvObject);
        pDriver->Release();
        return hr;
    }

    virtual HRESULT STDMETHODCALLTYPE LockServer(BOOL fLock) override
    {
        if (fLock)
            InterlockedIncrement(&g_serverLocks);
        else
            InterlockedDecrement(&g_serverLocks);
        return S_OK;
    }
};

static ProxyClassFactory g_factory;

// -----------------------------------------------------------------------------
// DllMain 진입점
// -----------------------------------------------------------------------------
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    (void)lpvReserved;

    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        g_hModule = hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);
    }
    return TRUE;
}

// -----------------------------------------------------------------------------
// COM 표준 익스포트 함수들
// -----------------------------------------------------------------------------
STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv)
{
    OutputDebugStringA("[ASIO_PROXY] DllGetClassObject called\n");

    if (rclsid != CLSID_ProxyAsio)
    {
        OutputDebugStringA("[ASIO_PROXY] DllGetClassObject: CLSID mismatch!\n");
        return CLASS_E_CLASSNOTAVAILABLE;
    }

    return g_factory.QueryInterface(riid, ppv);
}

STDAPI DllCanUnloadNow(void)
{
    return (g_serverLocks == 0) ? S_OK : S_FALSE;
}

// -----------------------------------------------------------------------------
// 레지스트리 자동 등록 (regsvr32)
// -----------------------------------------------------------------------------
STDAPI DllRegisterServer(void)
{
    char dllPath[MAX_PATH];
    GetModuleFileNameA(g_hModule, dllPath, MAX_PATH);

    // 1. CLSID 문자열화
    LPOLESTR clsidStrW = nullptr;
    StringFromCLSID(CLSID_ProxyAsio, &clsidStrW);
    char clsidStr[64];
    WideCharToMultiByte(CP_ACP, 0, clsidStrW, -1, clsidStr, sizeof(clsidStr), nullptr, nullptr);
    CoTaskMemFree(clsidStrW);

    // 2. HKCR\CLSID\{...}\InprocServer32 등록
    char keyBuf[256];
    HKEY hKey = nullptr;

    wsprintfA(keyBuf, "CLSID\\%s", clsidStr);
    RegCreateKeyA(HKEY_CLASSES_ROOT, keyBuf, &hKey);
    RegSetValueExA(hKey, nullptr, 0, REG_SZ, (const BYTE*)DRIVER_NAME, (DWORD)strlen(DRIVER_NAME) + 1);
    RegCloseKey(hKey);

    wsprintfA(keyBuf, "CLSID\\%s\\InprocServer32", clsidStr);
    RegCreateKeyA(HKEY_CLASSES_ROOT, keyBuf, &hKey);
    RegSetValueExA(hKey, nullptr, 0, REG_SZ, (const BYTE*)dllPath, (DWORD)strlen(dllPath) + 1);
    RegSetValueExA(hKey, "ThreadingModel", 0, REG_SZ, (const BYTE*)"Apartment", 10);
    RegCloseKey(hKey);

    // 3. HKLM\SOFTWARE\ASIO\Proxy ASIO (OBS Splitter) 등록 (게임이 인식하는 경로)
    wsprintfA(keyBuf, "SOFTWARE\\ASIO\\%s", DRIVER_NAME);
    if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, keyBuf, 0, nullptr, REG_OPTION_NON_VOLATILE,
                        KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
    {
        RegSetValueExA(hKey, "CLSID", 0, REG_SZ, (const BYTE*)clsidStr, (DWORD)strlen(clsidStr) + 1);
        RegSetValueExA(hKey, "Description", 0, REG_SZ, (const BYTE*)DRIVER_DESC, (DWORD)strlen(DRIVER_DESC) + 1);
        RegCloseKey(hKey);
    }

    return S_OK;
}

STDAPI DllUnregisterServer(void)
{
    LPOLESTR clsidStrW = nullptr;
    StringFromCLSID(CLSID_ProxyAsio, &clsidStrW);
    char clsidStr[64];
    WideCharToMultiByte(CP_ACP, 0, clsidStrW, -1, clsidStr, sizeof(clsidStr), nullptr, nullptr);
    CoTaskMemFree(clsidStrW);

    char keyBuf[256];
    wsprintfA(keyBuf, "CLSID\\%s\\InprocServer32", clsidStr);
    RegDeleteKeyA(HKEY_CLASSES_ROOT, keyBuf);

    wsprintfA(keyBuf, "CLSID\\%s", clsidStr);
    RegDeleteKeyA(HKEY_CLASSES_ROOT, keyBuf);

    wsprintfA(keyBuf, "SOFTWARE\\ASIO\\%s", DRIVER_NAME);
    RegDeleteKeyA(HKEY_LOCAL_MACHINE, keyBuf);

    return S_OK;
}
