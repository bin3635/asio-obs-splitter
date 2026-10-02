#pragma once

#ifndef WINDOWS
#define WINDOWS 1
#endif

#include <windows.h>
#include <objbase.h>

#include <string>
#include <vector>
#include <span>
#include <memory>
#include <thread>

#include "asiosys.h"
#include "asio.h"
#include "iasiodrv.h"

class IpcSender;

class ProxyAsio : public IASIO
{
public:
    ProxyAsio();
    virtual ~ProxyAsio();

    bool loadRealDriver(const CLSID& clsid);

    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override;
    virtual ULONG STDMETHODCALLTYPE AddRef(void) override;
    virtual ULONG STDMETHODCALLTYPE Release(void) override;

    virtual ASIOBool init(void* sysHandle) override;
    virtual void getDriverName(char* name) override;
    virtual long getDriverVersion() override;
    virtual void getErrorMessage(char* string) override;
    virtual ASIOError start() override;
    virtual ASIOError stop() override;
    virtual ASIOError getChannels(long* numInputChannels, long* numOutputChannels) override;
    virtual ASIOError getLatencies(long* inputLatency, long* outputLatency) override;
    virtual ASIOError getBufferSize(long* minSize, long* maxSize, long* preferredSize, long* granularity) override;
    virtual ASIOError canSampleRate(ASIOSampleRate sampleRate) override;
    virtual ASIOError getSampleRate(ASIOSampleRate* sampleRate) override;
    virtual ASIOError setSampleRate(ASIOSampleRate sampleRate) override;
    virtual ASIOError getClockSources(ASIOClockSource* clocks, long* numSources) override;
    virtual ASIOError setClockSource(long reference) override;
    virtual ASIOError getSamplePosition(ASIOSamples* sPos, ASIOTimeStamp* tStamp) override;
    virtual ASIOError getChannelInfo(ASIOChannelInfo* info) override;
    virtual ASIOError createBuffers(ASIOBufferInfo* bufferInfos, long numChannels, long bufferSize, ASIOCallbacks* callbacks) override;
    virtual ASIOError disposeBuffers() override;
    virtual ASIOError controlPanel() override;
    virtual ASIOError future(long selector, void* opt) override;
    virtual ASIOError outputReady() override;

    // Audient 콜백을 중계받을 정적 콜백 핸들러
    static void bufferSwitch(long doubleBufferIndex, ASIOBool directProcess);
    static void sampleRateChanged(ASIOSampleRate sRate);
    static long asioMessage(long selector, long value, void* message, double* opt);
    static ASIOTime* bufferSwitchTimeInfo(ASIOTime* params, long doubleBufferIndex, ASIOBool directProcess);

public:
    ASIOCallbacks* getHostCallbacks() const { return m_hostCallbacks; }
    void onBufferSwitch(long doubleBufferIndex, ASIOBool directProcess);
    ASIOTime* onBufferSwitchTimeInfo(ASIOTime* params, long doubleBufferIndex, ASIOBool directProcess);

private:
    void watchDriverSelection(std::stop_token stopToken);
    void releaseRealDriver();

    long m_refCount{1};
    IASIO* m_realDriver{nullptr};

    ASIOCallbacks* m_hostCallbacks{nullptr};
    ASIOCallbacks m_proxyCallbacks{};

    std::vector<ASIOBufferInfo> m_bufferInfos;
    long m_numChannels{0};
    long m_bufferSize{0};
    bool m_isRunning{false};
    bool m_buffersCreated{false};
    void* m_sysHandle{nullptr};

    std::unique_ptr<IpcSender> m_ipcSender;
    ASIOSampleType m_sampleType{ASIOSTInt32LSB};
    HANDLE m_hostConnectedEvent{nullptr};
    HANDLE m_selectionChangedEvent{nullptr};
    HANDLE m_controlPanelEvent{nullptr};
    CLSID m_selectedDriverClsid{};
    bool m_hasSelectedDriver{false};
    std::jthread m_selectionThread;
};
