#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <cstdint>
#include <algorithm>
#include "asio_ipc_protocol.h"
#include "asio.h"

class IpcSender
{
public:
    IpcSender();
    ~IpcSender();

    bool initialize();
    void shutdown();

    void updateFormat(uint32_t sampleRate, uint32_t channels);
    void setStreaming(bool streaming);

    // 오디오 콜백 스레드에서 직접 호출되는 빠른 데이터 복제 메서드
    void pushAudio(long doubleBufferIndex, ASIOBufferInfo* bufferInfos, long numChannels, long bufferSize, ASIOSampleType sampleType);

private:
    HANDLE m_hMapFile{nullptr};
    HANDLE m_hDataReadyEvent{nullptr};
    AsioSplitter::Ipc::SharedMemoryLayout* m_sharedMem{nullptr};

    uint64_t m_frameCounter{0};
    uint32_t m_currentWriteIndex{0};

    // ASIO 버퍼의 샘플 형식을 OBS가 사용하는 Float32로 변환한다.
    static void convertToFloat(void* src, float* dst, long sampleCount, ASIOSampleType type);
};
