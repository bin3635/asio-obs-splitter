#include "ipc_receiver.h"
#include <array>

IpcReceiver::IpcReceiver() = default;

IpcReceiver::~IpcReceiver()
{
    stop();
}

bool IpcReceiver::start(AudioCallback cb)
{
    stop();
    m_audioCallback = std::move(cb);
    m_lastReadIndex = 0;

    // OBS가 먼저 실행될 수 있으므로, 게임이 IPC 객체를 만들 때까지 워커가 재연결한다.
    m_connected.store(true, std::memory_order_release);
    m_workerThread = std::jthread([this](std::stop_token st) { workerLoop(st); });

    return true;
}

bool IpcReceiver::connectToSender()
{
    if (m_sharedMem)
    {
        return true;
    }

    m_hMapFile = OpenFileMappingA(
        FILE_MAP_READ | FILE_MAP_WRITE,
        FALSE,
        AsioSplitter::Ipc::SHARED_MEMORY_NAME
    );
    if (!m_hMapFile)
    {
        char logBuf[128];
        sprintf_s(logBuf, "[ASIO_PROXY] OpenFileMapping failed: %lu\n",
                  static_cast<unsigned long>(GetLastError()));
        OutputDebugStringA(logBuf);
        return false;
    }

    m_sharedMem = static_cast<AsioSplitter::Ipc::SharedMemoryLayout*>(
        MapViewOfFile(m_hMapFile, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, AsioSplitter::Ipc::SHARED_MEMORY_SIZE)
    );

    if (!m_sharedMem)
    {
        char logBuf[128];
        sprintf_s(logBuf, "[ASIO_PROXY] MapViewOfFile failed: %lu\n",
                  static_cast<unsigned long>(GetLastError()));
        OutputDebugStringA(logBuf);
        CloseHandle(m_hMapFile);
        m_hMapFile = nullptr;
        return false;
    }

    m_hDataReadyEvent = OpenEventA(
        SYNCHRONIZE | EVENT_MODIFY_STATE,
        FALSE,
        AsioSplitter::Ipc::EVENT_DATA_READY_NAME
    );
    if (!m_hDataReadyEvent)
    {
        char logBuf[128];
        sprintf_s(logBuf, "[ASIO_PROXY] OpenEvent failed: %lu\n",
                  static_cast<unsigned long>(GetLastError()));
        OutputDebugStringA(logBuf);
    }
    return true;
}

void IpcReceiver::stop()
{
    m_connected.store(false, std::memory_order_release);

    if (m_workerThread.joinable())
    {
        m_workerThread.request_stop();
        // 이벤트 대기에서 깨어나도록 펄스 전달
        if (m_hDataReadyEvent)
        {
            SetEvent(m_hDataReadyEvent);
        }
        m_workerThread.join();
    }

    if (m_sharedMem)
    {
        UnmapViewOfFile(m_sharedMem);
        m_sharedMem = nullptr;
    }

    if (m_hMapFile)
    {
        CloseHandle(m_hMapFile);
        m_hMapFile = nullptr;
    }

    if (m_hDataReadyEvent)
    {
        CloseHandle(m_hDataReadyEvent);
        m_hDataReadyEvent = nullptr;
    }
}

void IpcReceiver::workerLoop(std::stop_token stopToken)
{
    std::array<float, AsioSplitter::Ipc::MAX_IPC_CHANNELS * AsioSplitter::Ipc::MAX_SAMPLES_PER_BUFFER> sampleCopy{};

    while (!stopToken.stop_requested())
    {
        if (!connectToSender())
        {
            Sleep(100);
            continue;
        }

        if (stopToken.stop_requested() || !m_sharedMem)
        {
            break;
        }

        // 송신자가 아직 스트리밍하지 않으면 이벤트를 기다렸다가 다시 확인한다.
        if (!m_sharedMem->header.isStreaming.load(std::memory_order_acquire) ||
            m_sharedMem->header.magic != AsioSplitter::Ipc::IPC_MAGIC)
        {
            if (m_hDataReadyEvent)
            {
                WaitForSingleObject(m_hDataReadyEvent, 100);
            }
            else
            {
                Sleep(5);
            }
            continue;
        }

        uint32_t currentWrite = m_sharedMem->header.writeIndex.load(std::memory_order_acquire);

        // 게임이 ASIO 장치를 다시 열면 송신자가 인덱스를 0부터 재시작한다.
        // 이전 세션의 readIndex를 그대로 사용하면 receiver가 존재하지 않는
        // 슬롯만 기다리게 되므로, producer의 인덱스가 되감긴 것을 새 세션으로 처리한다.
        if (currentWrite < m_lastReadIndex &&
            m_lastReadIndex - currentWrite > AsioSplitter::Ipc::RING_BUFFER_SLOTS)
        {
            m_lastReadIndex = 0;
            m_sharedMem->header.readIndex.store(0, std::memory_order_release);
            continue;
        }

        if (currentWrite == m_lastReadIndex)
        {
            // 큐가 비어 있을 때만 이벤트를 기다린다. 데이터가 쌓여 있으면
            // 이벤트를 다시 기다리지 않고 연속으로 소비해야 작은 ASIO 블록을 놓치지 않는다.
            if (m_hDataReadyEvent)
            {
                WaitForSingleObject(m_hDataReadyEvent, 100);
            }
            else
            {
                Sleep(5);
            }
            continue;
        }

        // 가장 오래된 슬롯부터 읽는다. readIndex는 복사가 끝난 뒤에만 증가한다.
        uint32_t slotIdx = m_lastReadIndex & AsioSplitter::Ipc::SLOT_MASK;
        const auto& slot = m_sharedMem->slots[slotIdx];

        if (slot.state.load(std::memory_order_acquire) == AsioSplitter::Ipc::SLOT_READY)
        {
            uint32_t channels = std::min(slot.channels, AsioSplitter::Ipc::MAX_IPC_CHANNELS);
            uint32_t sampleCount = std::min(slot.sampleCount, AsioSplitter::Ipc::MAX_SAMPLES_PER_BUFFER);
            uint32_t sampleRate = m_sharedMem->header.sampleRate;
            float* copyPointers[AsioSplitter::Ipc::MAX_IPC_CHANNELS];
            const float* channelPointers[AsioSplitter::Ipc::MAX_IPC_CHANNELS];
            for (uint32_t ch = 0; ch < channels; ++ch)
            {
                copyPointers[ch] = sampleCopy.data() + (ch * AsioSplitter::Ipc::MAX_SAMPLES_PER_BUFFER);
                std::copy_n(slot.samples[ch], sampleCount, copyPointers[ch]);
                channelPointers[ch] = copyPointers[ch];
            }

            // 공유 메모리 포인터를 OBS 콜백에 넘기지 않는다. 먼저 로컬 배열로 복사한 뒤
            // 소비 위치를 공개해야 송신자가 같은 슬롯을 재사용해도 안전하다.
            m_lastReadIndex++;
            m_sharedMem->header.readIndex.store(m_lastReadIndex, std::memory_order_release);

            if (m_audioCallback && sampleCount > 0)
            {
                m_audioCallback(
                    channelPointers,
                    channels,
                    sampleCount,
                    sampleRate
                );
            }
        }
        else
        {
            Sleep(1);
        }
    }
}
