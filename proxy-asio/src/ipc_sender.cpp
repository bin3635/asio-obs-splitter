#include "ipc_sender.h"
#include <algorithm>

IpcSender::IpcSender() = default;

IpcSender::~IpcSender()
{
    shutdown();
}

bool IpcSender::initialize()
{
    SECURITY_DESCRIPTOR securityDescriptor{};
    if (!InitializeSecurityDescriptor(&securityDescriptor, SECURITY_DESCRIPTOR_REVISION) ||
        !SetSecurityDescriptorDacl(&securityDescriptor, TRUE, nullptr, FALSE))
    {
        return false;
    }
    SECURITY_ATTRIBUTES securityAttributes{
        sizeof(SECURITY_ATTRIBUTES),
        &securityDescriptor,
        FALSE
    };

    // 게임과 OBS는 서로 다른 프로세스이므로 이름 있는 IPC 객체의 접근 권한을 명시한다.
    m_hMapFile = CreateFileMappingA(
        INVALID_HANDLE_VALUE,
        &securityAttributes,
        PAGE_READWRITE,
        0,
        static_cast<DWORD>(AsioSplitter::Ipc::SHARED_MEMORY_SIZE),
        AsioSplitter::Ipc::SHARED_MEMORY_NAME
    );

    if (!m_hMapFile) return false;

    m_sharedMem = static_cast<AsioSplitter::Ipc::SharedMemoryLayout*>(
        MapViewOfFile(m_hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, AsioSplitter::Ipc::SHARED_MEMORY_SIZE)
    );

    if (!m_sharedMem)
    {
        CloseHandle(m_hMapFile);
        m_hMapFile = nullptr;
        return false;
    }

    // 새 스트림이 시작될 때 이전 세션의 인덱스와 슬롯 상태를 초기화한다.
    m_sharedMem->header.magic = AsioSplitter::Ipc::IPC_MAGIC;
    m_sharedMem->header.version = AsioSplitter::Ipc::IPC_VERSION;
    m_sharedMem->header.sampleRate = 44100;
    m_sharedMem->header.activeChannels = 2;
    m_sharedMem->header.writeIndex.store(0, std::memory_order_relaxed);
    m_sharedMem->header.readIndex.store(0, std::memory_order_relaxed);
    m_sharedMem->header.isStreaming.store(false, std::memory_order_relaxed);
    for (auto& slot : m_sharedMem->slots)
    {
        slot.state.store(AsioSplitter::Ipc::SLOT_FREE, std::memory_order_relaxed);
    }

    m_hDataReadyEvent = CreateEventA(
        &securityAttributes,
        FALSE,
        FALSE,
        AsioSplitter::Ipc::EVENT_DATA_READY_NAME
    );

    if (!m_hDataReadyEvent)
    {
        UnmapViewOfFile(m_sharedMem);
        m_sharedMem = nullptr;
        CloseHandle(m_hMapFile);
        m_hMapFile = nullptr;
        return false;
    }

    return true;
}

void IpcSender::shutdown()
{
    setStreaming(false);

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

void IpcSender::updateFormat(uint32_t sampleRate, uint32_t channels)
{
    if (!m_sharedMem) return;
    m_sharedMem->header.sampleRate = sampleRate;
    m_sharedMem->header.activeChannels = std::min(channels, AsioSplitter::Ipc::MAX_IPC_CHANNELS);
}

void IpcSender::setStreaming(bool streaming)
{
    if (m_sharedMem)
    {
        m_sharedMem->header.isStreaming.store(streaming, std::memory_order_release);
    }
}

// ASIO의 다양한 버퍼 포맷을 OBS가 즉시 소비 가능한 [-1.0f, 1.0f] Float32로 정규화 변환
void IpcSender::convertToFloat(void* src, float* dst, long sampleCount, ASIOSampleType type)
{
    if (!src || !dst) return;

    switch (type)
    {
    case ASIOSTInt32LSB:
    {
        auto* in = static_cast<int32_t*>(src);
        for (long i = 0; i < sampleCount; ++i)
        {
            dst[i] = static_cast<float>(in[i]) / 2147483648.0f;
        }
        break;
    }
    case ASIOSTInt32LSB16:
    case ASIOSTInt32LSB18:
    case ASIOSTInt32LSB20:
    case ASIOSTInt32LSB24:
    {
        auto* in = static_cast<int32_t*>(src);
        for (long i = 0; i < sampleCount; ++i)
        {
            dst[i] = static_cast<float>(in[i]) / 2147483648.0f;
        }
        break;
    }
    case ASIOSTInt24LSB:
    {
        auto* in = static_cast<uint8_t*>(src);
        for (long i = 0; i < sampleCount; ++i)
        {
            int32_t val = (in[i * 3 + 2] << 24) | (in[i * 3 + 1] << 16) | (in[i * 3 + 0] << 8);
            dst[i] = static_cast<float>(val) / 2147483648.0f;
        }
        break;
    }
    case ASIOSTInt16LSB:
    {
        auto* in = static_cast<int16_t*>(src);
        for (long i = 0; i < sampleCount; ++i)
        {
            dst[i] = static_cast<float>(in[i]) / 32768.0f;
        }
        break;
    }
    case ASIOSTFloat32LSB:
    {
        auto* in = static_cast<float*>(src);
        std::copy_n(in, sampleCount, dst);
        break;
    }
    case ASIOSTFloat64LSB:
    {
        auto* in = static_cast<double*>(src);
        for (long i = 0; i < sampleCount; ++i)
        {
            dst[i] = static_cast<float>(in[i]);
        }
        break;
    }
    default:
        std::fill_n(dst, sampleCount, 0.0f);
        break;
    }
}

void IpcSender::pushAudio(long doubleBufferIndex, ASIOBufferInfo* bufferInfos, long numChannels, long bufferSize, ASIOSampleType sampleType)
{
    if (!m_sharedMem || bufferSize > AsioSplitter::Ipc::MAX_SAMPLES_PER_BUFFER) return;

    // readIndex는 OBS가 복사까지 끝낸 위치다. 큐가 가득 차면 이 블록을 버린다.
    uint32_t readIndex = m_sharedMem->header.readIndex.load(std::memory_order_acquire);
    if (m_currentWriteIndex - readIndex >= AsioSplitter::Ipc::RING_BUFFER_SLOTS)
    {
        return;
    }

    uint32_t slotIdx = m_currentWriteIndex & AsioSplitter::Ipc::SLOT_MASK;
    auto& slot = m_sharedMem->slots[slotIdx];

    slot.state.store(AsioSplitter::Ipc::SLOT_WRITING, std::memory_order_relaxed);
    slot.frameIndex = ++m_frameCounter;
    slot.sampleCount = static_cast<uint32_t>(bufferSize);

    uint32_t outputChannelCount = 0;

    // 게임 출력 버퍼만 캡처한다. 현재 OBS 소스는 스테레오이므로 최대 두 채널이다.
    for (long i = 0; i < numChannels; ++i)
    {
        if (bufferInfos[i].isInput == ASIOFalse)
        {
            if (outputChannelCount < 2)
            {
                void* pAudioData = bufferInfos[i].buffers[doubleBufferIndex];
                convertToFloat(pAudioData, slot.samples[outputChannelCount], bufferSize, sampleType);
                outputChannelCount++;
            }
        }
    }
    slot.channels = outputChannelCount;

    // 슬롯 기록 완료 표시 및 링 버퍼 헤더 갱신
    slot.state.store(AsioSplitter::Ipc::SLOT_READY, std::memory_order_release);
    m_sharedMem->header.writeIndex.store(m_currentWriteIndex + 1, std::memory_order_release);
    m_currentWriteIndex++;

    // OBS 플러그인 워커 깨우기
    if (m_hDataReadyEvent)
    {
        SetEvent(m_hDataReadyEvent);
    }
}
