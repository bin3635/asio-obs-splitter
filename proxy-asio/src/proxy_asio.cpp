#include "proxy_asio.h"
#include "ipc_sender.h"
#include "asio_driver_config.h"
#include <cstring>
#include <objbase.h>
#include <guiddef.h>
#include <algorithm>
#include <string>

#ifndef INITGUID
const GUID IID_IASIO = { 0x4b7ab700, 0x33fb, 0x11d1, { 0x9b, 0x07, 0x00, 0x60, 0x97, 0x96, 0x47, 0x2b } };
#endif

static ProxyAsio* g_proxyInstance = nullptr;

static void launchTraySelector()
{
    wchar_t programFiles[MAX_PATH]{};
    DWORD length = GetEnvironmentVariableW(L"ProgramW6432", programFiles, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
    {
        length = GetEnvironmentVariableW(L"ProgramFiles", programFiles, MAX_PATH);
    }
    if (length == 0 || length >= MAX_PATH)
    {
        return;
    }

    std::wstring trayPath(programFiles);
    trayPath += L"\\ASIO OBS Splitter\\AsioSplitterTray.exe";

    STARTUPINFOW startupInfo{sizeof(startupInfo)};
    PROCESS_INFORMATION processInfo{};
    if (CreateProcessW(trayPath.c_str(), nullptr, nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW, nullptr, nullptr, &startupInfo, &processInfo))
    {
        CloseHandle(processInfo.hThread);
        CloseHandle(processInfo.hProcess);
    }
}

static void staticBufferSwitch(long doubleBufferIndex, ASIOBool directProcess)
{
    if (g_proxyInstance)
    {
        g_proxyInstance->onBufferSwitch(doubleBufferIndex, directProcess);
    }
}

static void staticSampleRateDidChange(ASIOSampleRate sRate)
{
    if (g_proxyInstance && g_proxyInstance->getHostCallbacks())
    {
        auto* cb = g_proxyInstance->getHostCallbacks();
        if (cb->sampleRateDidChange)
        {
            cb->sampleRateDidChange(sRate);
        }
    }
}

static long staticAsioMessage(long selector, long value, void* message, double* opt)
{
    if (g_proxyInstance && g_proxyInstance->getHostCallbacks())
    {
        auto* cb = g_proxyInstance->getHostCallbacks();
        if (cb->asioMessage)
        {
            return cb->asioMessage(selector, value, message, opt);
        }
    }
    return 0;
}

static ASIOTime* staticBufferSwitchTimeInfo(ASIOTime* params, long doubleBufferIndex, ASIOBool directProcess)
{
    if (g_proxyInstance)
    {
        return g_proxyInstance->onBufferSwitchTimeInfo(params, doubleBufferIndex, directProcess);
    }
    return nullptr;
}

ProxyAsio::ProxyAsio()
    : m_ipcSender(std::make_unique<IpcSender>())
    , m_refCount(1)
    , m_realDriver(nullptr)
    , m_hostCallbacks(nullptr)
    , m_numChannels(0)
    , m_bufferSize(0)
    , m_isRunning(false)
{
    g_proxyInstance = this;
    std::memset(&m_proxyCallbacks, 0, sizeof(m_proxyCallbacks));
}

ProxyAsio::~ProxyAsio()
{
    if (m_selectionThread.joinable())
    {
        m_selectionThread.request_stop();
        if (m_selectionChangedEvent)
        {
            SetEvent(m_selectionChangedEvent);
        }
        if (m_controlPanelEvent)
        {
            SetEvent(m_controlPanelEvent);
        }
        m_selectionThread.join();
    }

    if (m_selectionChangedEvent)
    {
        CloseHandle(m_selectionChangedEvent);
        m_selectionChangedEvent = nullptr;
    }

    if (m_controlPanelEvent)
    {
        CloseHandle(m_controlPanelEvent);
        m_controlPanelEvent = nullptr;
    }

    if (m_hostConnectedEvent)
    {
        ResetEvent(m_hostConnectedEvent);
        CloseHandle(m_hostConnectedEvent);
        m_hostConnectedEvent = nullptr;
    }

    releaseRealDriver();

    if (g_proxyInstance == this)
    {
        g_proxyInstance = nullptr;
    }
}

void ProxyAsio::releaseRealDriver()
{
    if (!m_realDriver)
    {
        return;
    }

    if (m_isRunning)
    {
        m_realDriver->stop();
        m_isRunning = false;
    }
    if (m_buffersCreated)
    {
        m_realDriver->disposeBuffers();
        m_buffersCreated = false;
    }

    m_realDriver->Release();
    m_realDriver = nullptr;
}

// 오디오 인터페이스 드라이버 로드
bool ProxyAsio::loadRealDriver(const CLSID& clsid)
{
    if (m_realDriver)
    {
        m_realDriver->Release();
        m_realDriver = nullptr;
    }

    HRESULT hr = CoCreateInstance(
        clsid,
        nullptr,
        CLSCTX_INPROC_SERVER,
        clsid,
        reinterpret_cast<void**>(&m_realDriver)
    );

    if (FAILED(hr))
    {
        char logBuf[128];
        sprintf_s(logBuf, "[ASIO_PROXY] CoCreateInstance failed: 0x%08lX\n",
                  static_cast<unsigned long>(hr));
        OutputDebugStringA(logBuf);
    }

    return SUCCEEDED(hr) && (m_realDriver != nullptr);
}

static const GUID CLSID_PROXY_DRIVER = 
    { 0xA5C8E531, 0x9F22, 0x4D9A, { 0x8C, 0x37, 0xF7, 0x95, 0x26, 0xC8, 0xD8, 0xE1 } };

HRESULT STDMETHODCALLTYPE ProxyAsio::QueryInterface(REFIID riid, void** ppvObject)
{
    if (!ppvObject)
    {
        return E_POINTER;
    }

    // IUnknown, 표준 IASIO, 그리고 호스트가 요청한 드라이버 CLSID 허용
    if (IsEqualGUID(riid, IID_IUnknown) || 
        IsEqualGUID(riid, IID_IASIO) || 
        IsEqualGUID(riid, CLSID_PROXY_DRIVER))
    {
        *ppvObject = static_cast<IASIO*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE ProxyAsio::AddRef()
{
    return InterlockedIncrement(&m_refCount);
}

ULONG STDMETHODCALLTYPE ProxyAsio::Release()
{
    ULONG count = InterlockedDecrement(&m_refCount);
    if (count == 0)
    {
        delete this;
    }
    return count;
}

ASIOBool ProxyAsio::init(void* sysHandle)
{
    OutputDebugStringA("[ASIO_PROXY] init called\n");
    m_sysHandle = sysHandle;

    CLSID selectedClsid{};
    if (!AsioSplitter::Drivers::readSelected(selectedClsid))
    {
        const auto drivers = AsioSplitter::Drivers::enumerate();
        if (drivers.empty())
        {
            OutputDebugStringA("[ASIO_PROXY] No registered ASIO drivers found\n");
            return ASIOFalse;
        }
        selectedClsid = drivers.front().clsid;
        AsioSplitter::Drivers::writeSelected(selectedClsid);
    }

    if (m_realDriver && (!m_hasSelectedDriver || !IsEqualCLSID(m_selectedDriverClsid, selectedClsid)))
    {
        releaseRealDriver();
    }

    if (!m_realDriver && !loadRealDriver(selectedClsid))
    {
        OutputDebugStringA("[ASIO_PROXY] Audient ASIO could not be loaded\n");
        return ASIOFalse;
    }

    m_selectedDriverClsid = selectedClsid;
    m_hasSelectedDriver = true;

    if (m_realDriver->init(sysHandle) == ASIOFalse)
    {
        OutputDebugStringA("[ASIO_PROXY] Audient ASIO init failed\n");
        releaseRealDriver();
        return ASIOFalse;
    }

    // 트레이 앱은 이 이벤트가 신호 상태일 때만 아이콘을 표시한다.
    m_hostConnectedEvent = CreateEventA(
        nullptr,
        TRUE,
        TRUE,
        AsioSplitter::Ipc::EVENT_HOST_CONNECTED_NAME
    );
    if (!m_hostConnectedEvent)
    {
        m_realDriver->Release();
        m_realDriver = nullptr;
        return ASIOFalse;
    }

    if (!m_selectionChangedEvent)
    {
        m_selectionChangedEvent = CreateEventA(
            nullptr,
            FALSE,
            FALSE,
            AsioSplitter::Ipc::EVENT_DRIVER_SELECTION_CHANGED_NAME
        );
        if (!m_selectionChangedEvent)
        {
            return ASIOFalse;
        }
    }

    if (!m_controlPanelEvent)
    {
        m_controlPanelEvent = CreateEventA(
            nullptr,
            FALSE,
            FALSE,
            AsioSplitter::Ipc::EVENT_CONTROL_PANEL_REQUEST_NAME
        );
        if (!m_controlPanelEvent)
        {
            return ASIOFalse;
        }
    }

    if (!m_selectionThread.joinable())
    {
        m_selectionThread = std::jthread([this](std::stop_token stopToken) {
            watchDriverSelection(stopToken);
        });
    }

    launchTraySelector();

    return ASIOTrue;
}

void ProxyAsio::watchDriverSelection(std::stop_token stopToken)
{
    while (!stopToken.stop_requested())
    {
        HANDLE events[] = {m_selectionChangedEvent, m_controlPanelEvent};
        const DWORD result = WaitForMultipleObjects(2, events, FALSE, INFINITE);
        if (result == WAIT_FAILED || stopToken.stop_requested())
        {
            break;
        }

        if (result == WAIT_OBJECT_0)
        {
            // ASIO 드라이버를 실행 중에 교체하지 않고, 호스트에게 표준 reset 요청을
            // 보내 호스트가 stop/dispose/init/createBuffers 순서로 다시 열게 한다.
            if (m_hostCallbacks && m_hostCallbacks->asioMessage)
            {
                m_hostCallbacks->asioMessage(kAsioResetRequest, 0, nullptr, nullptr);
            }
        }
        else if (result == WAIT_OBJECT_0 + 1 && m_realDriver)
        {
            m_realDriver->controlPanel();
        }
    }
}

void ProxyAsio::getDriverName(char* name)
{
    if (name)
    {
        // 널 종료 포함 안전하게 복사
        strncpy_s(name, 32, "Proxy ASIO (OBS Splitter)", _TRUNCATE);
    }
}

long ProxyAsio::getDriverVersion()
{
    return m_realDriver ? m_realDriver->getDriverVersion() : 1;
}

void ProxyAsio::getErrorMessage(char *string)
{
    if (m_realDriver)
    {
        m_realDriver->getErrorMessage(string);
    }
    else if (string)
    {
        strcpy_s(string, 128, "No ASIO driver loaded");
    }
}

ASIOError ProxyAsio::start()
{
    if (!m_realDriver && init(m_sysHandle) == ASIOFalse)
    {
        return ASE_NotPresent;
    }

    ASIOSampleRate sampleRate = 0.0;
    long inputChannels = 0;
    long outputChannels = 0;
    if (m_realDriver->getSampleRate(&sampleRate) != ASE_OK ||
        m_realDriver->getChannels(&inputChannels, &outputChannels) != ASE_OK)
    {
        return ASE_HWMalfunction;
    }

    if (!m_ipcSender->initialize())
    {
        return ASE_NoMemory;
    }

    m_ipcSender->updateFormat(
        static_cast<uint32_t>(sampleRate + 0.5),
        static_cast<uint32_t>(std::min(outputChannels, 2L)));

    ASIOError err = m_realDriver->start();
    if (err == ASE_OK)
    {
        m_isRunning = true;
        m_ipcSender->setStreaming(true);
    }
    else
    {
        m_ipcSender->shutdown();
    }
    return err;
}

ASIOError ProxyAsio::stop()
{
    if (!m_realDriver) return ASE_NotPresent;

    m_isRunning = false;
    m_ipcSender->setStreaming(false);
    ASIOError err = m_realDriver->stop();
    m_ipcSender->shutdown();
    return err;
}

ASIOError ProxyAsio::getChannels(long* numInputChannels, long* numOutputChannels)
{
    return m_realDriver
        ? m_realDriver->getChannels(numInputChannels, numOutputChannels)
        : ASE_NotPresent;
}

ASIOError ProxyAsio::getLatencies(long* inputLatency, long* outputLatency)
{
    return m_realDriver ? m_realDriver->getLatencies(inputLatency, outputLatency) : ASE_NotPresent;
}

ASIOError ProxyAsio::getBufferSize(long* minSize, long* maxSize, long* preferredSize, long* granularity)
{
    // 실제 드라이버를 래핑 중이라면 실제 드라이버 값 위임
    if (m_realDriver)
    {
        ASIOError err = m_realDriver->getBufferSize(minSize, maxSize, preferredSize, granularity);
        return err;
    }

    // 단독/더미 모드일 때 표준적인 유효값 설정
    if (minSize)       *minSize = 64;
    if (maxSize)       *maxSize = 2048;
    if (preferredSize) *preferredSize = 256;  // 게임 기본값으로 흔히 쓰이는 256
    if (granularity)   *granularity = -1;     // -1: 2의 거듭제곱 단위 (Power of 2), 또는 0: min~max 사이 자유

    return ASE_OK; // 0
}

ASIOError ProxyAsio::canSampleRate(ASIOSampleRate sampleRate)
{
    return m_realDriver ? m_realDriver->canSampleRate(sampleRate) : ASE_NoClock;
}

ASIOError ProxyAsio::getSampleRate(ASIOSampleRate* sampleRate)
{
    OutputDebugStringA("[ASIO_PROXY] getSampleRate called\n");

    return m_realDriver ? m_realDriver->getSampleRate(sampleRate) : ASE_NotPresent;
}

ASIOError ProxyAsio::setSampleRate(ASIOSampleRate sampleRate)
{
    return m_realDriver ? m_realDriver->setSampleRate(sampleRate) : ASE_NoClock;
}

ASIOError ProxyAsio::getClockSources(ASIOClockSource* clocks, long* numSources)
{
    return m_realDriver ? m_realDriver->getClockSources(clocks, numSources) : ASE_NotPresent;
}

ASIOError ProxyAsio::setClockSource(long reference)
{
    return m_realDriver ? m_realDriver->setClockSource(reference) : ASE_NotPresent;
}

ASIOError ProxyAsio::getSamplePosition(ASIOSamples* sPos, ASIOTimeStamp* tStamp)
{
    return m_realDriver ? m_realDriver->getSamplePosition(sPos, tStamp) : ASE_NotPresent;
}

ASIOError ProxyAsio::getChannelInfo(ASIOChannelInfo* info)
{
    return m_realDriver ? m_realDriver->getChannelInfo(info) : ASE_NotPresent;
}

ASIOError ProxyAsio::disposeBuffers()
{
    if (!m_realDriver)
    {
        return ASE_NotPresent;
    }

    ASIOError err = m_realDriver->disposeBuffers();
    m_buffersCreated = false;
    m_bufferInfos.clear();
    m_numChannels = 0;
    m_bufferSize = 0;

    return err;
}

ASIOError ProxyAsio::controlPanel()
{
    return m_realDriver ? m_realDriver->controlPanel() : ASE_NotPresent;
}

ASIOError ProxyAsio::future(long selector, void* opt)
{
    return m_realDriver ? m_realDriver->future(selector, opt) : ASE_NotPresent;
}

ASIOError ProxyAsio::outputReady()
{
    return m_realDriver ? m_realDriver->outputReady() : ASE_NotPresent;
}

ASIOError ProxyAsio::createBuffers(ASIOBufferInfo* bufferInfos, long numChannels, long bufferSize, ASIOCallbacks* callbacks)
{
    if (!m_realDriver || !bufferInfos || numChannels <= 0 || !callbacks || bufferSize <= 0)
    {
        return ASE_InvalidParameter;
    }

    // 1. 게임(호스트)이 넘겨준 원본 콜백 함수 백업
    m_hostCallbacks = callbacks;
    m_numChannels = numChannels;
    m_bufferSize = bufferSize;

    // 실제 드라이버에는 프록시 콜백을 등록하고, 호스트 콜백은 멤버로 보관한다.
    m_proxyCallbacks.bufferSwitch = &staticBufferSwitch;
    m_proxyCallbacks.sampleRateDidChange = &staticSampleRateDidChange;
    m_proxyCallbacks.asioMessage = &staticAsioMessage;
    m_proxyCallbacks.bufferSwitchTimeInfo = &staticBufferSwitchTimeInfo;

    ASIOError err = m_realDriver->createBuffers(bufferInfos, numChannels, bufferSize, &m_proxyCallbacks);
    if (err != ASE_OK)
    {
        m_hostCallbacks = nullptr;
        m_bufferInfos.clear();
        m_numChannels = 0;
        m_bufferSize = 0;
    }
    else
    {
        // 버퍼 주소는 createBuffers가 반환될 때 실제 드라이버가 채운 값으로 복사한다.
        m_bufferInfos.assign(bufferInfos, bufferInfos + numChannels);

        for (const auto& bufferInfo : m_bufferInfos)
        {
            if (bufferInfo.isInput == ASIOFalse)
            {
                ASIOChannelInfo channelInfo{};
                channelInfo.channel = bufferInfo.channelNum;
                channelInfo.isInput = ASIOFalse;
                if (m_realDriver->getChannelInfo(&channelInfo) == ASE_OK)
                {
                    m_sampleType = channelInfo.type;
                    break;
                }
            }
        }
        m_buffersCreated = true;
    }
    return err;
}

void ProxyAsio::onBufferSwitch(long doubleBufferIndex, ASIOBool directProcess)
{
    // 1. 호스트(게임) 먼저 렌더링
    if (m_hostCallbacks && m_hostCallbacks->bufferSwitch)
    {
        m_hostCallbacks->bufferSwitch(doubleBufferIndex, directProcess);
    }

    // 2. 가로채서 IPC로 전송
    m_ipcSender->pushAudio(doubleBufferIndex, m_bufferInfos.data(), m_numChannels, m_bufferSize, m_sampleType);
}

ASIOTime* ProxyAsio::onBufferSwitchTimeInfo(ASIOTime* params, long doubleBufferIndex, ASIOBool directProcess)
{
    ASIOTime* result = nullptr;
    if (m_hostCallbacks && m_hostCallbacks->bufferSwitchTimeInfo)
    {
        result = m_hostCallbacks->bufferSwitchTimeInfo(params, doubleBufferIndex, directProcess);
    }

    m_ipcSender->pushAudio(doubleBufferIndex, m_bufferInfos.data(), m_numChannels, m_bufferSize, m_sampleType);
    return result;
}
