#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <thread>
#include <atomic>
#include <functional>
#include "asio_ipc_protocol.h"

class IpcReceiver
{
public:
    using AudioCallback = std::function<void(const float* const* channelData, uint32_t channels, uint32_t sampleCount, uint32_t sampleRate)>;

    IpcReceiver();
    ~IpcReceiver();

    bool start(AudioCallback cb);
    void stop();

    bool isConnected() const { return m_connected.load(std::memory_order_relaxed); }

private:
    bool connectToSender();
    void workerLoop(std::stop_token stopToken);

    HANDLE m_hMapFile{nullptr};
    HANDLE m_hDataReadyEvent{nullptr};
    AsioSplitter::Ipc::SharedMemoryLayout* m_sharedMem{nullptr};

    std::atomic<bool> m_connected{false};
    std::jthread m_workerThread;
    AudioCallback m_audioCallback;

    uint32_t m_lastReadIndex{0};
};
