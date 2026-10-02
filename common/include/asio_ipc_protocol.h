#pragma once

#include <cstdint>
#include <atomic>

namespace AsioSplitter::Ipc
{
    // IPC 객체 식별용 이름 (Windows Local 네임스페이스)
    inline constexpr const char* SHARED_MEMORY_NAME = "Local\\AsioObsSplitter_SharedMemory";
    inline constexpr const char* EVENT_DATA_READY_NAME = "Local\\AsioObsSplitter_DataReady";
    inline constexpr const char* EVENT_HOST_CONNECTED_NAME = "Local\\AsioObsSplitter_HostConnected";
    inline constexpr const char* EVENT_DRIVER_SELECTION_CHANGED_NAME = "Local\\AsioObsSplitter_DriverSelectionChanged";
    inline constexpr const char* EVENT_CONTROL_PANEL_REQUEST_NAME = "Local\\AsioObsSplitter_ControlPanelRequest";

    // 프로토콜 매직 넘버 및 버전
    inline constexpr uint32_t IPC_MAGIC = 0x4153494F; // 'ASIO'
    inline constexpr uint32_t IPC_VERSION = 1;

    // 슬롯 및 채널 제약 조건
    inline constexpr uint32_t MAX_IPC_CHANNELS = 8;          // 기본 2ch, 최대 8ch 지원
    inline constexpr uint32_t MAX_SAMPLES_PER_BUFFER = 4096; // ASIO 버퍼 최대 크기 허용치
    inline constexpr uint32_t RING_BUFFER_SLOTS = 16;    // 순환 링 버퍼 슬롯 개수 (2^N)
    inline constexpr uint32_t SLOT_MASK = RING_BUFFER_SLOTS - 1;

    // 슬롯 상태 플래그
    enum SlotState : uint32_t
    {
        SLOT_FREE = 0,    // 비어 있음 (Producer가 쓸 수 있음)
        SLOT_WRITING = 1, // Producer가 데이터 기록 중
        SLOT_READY = 2,   // 기록 완료 (Consumer가 읽을 수 있음)
        SLOT_READING = 3  // Consumer가 읽는 중
    };

    // 개별 오디오 프레임 슬롯 (Planar Float32 구조)
    #pragma pack(push, 4)
    struct AudioSlot
    {
        std::atomic<uint32_t> state{SLOT_FREE};
        uint64_t frameIndex{0};                         // 프레임 순번 (패킷 드롭 감지용)
        uint32_t sampleCount{0};                        // 실제 버퍼 샘플 수 (예: 64, 128, 256 등)
        uint32_t channels{2};
        
        // 채널별 평면(Planar) Float32 샘플 데이터
        // OBS Studio의 obs_source_output_audio는 기본적으로 Float32 planar 배열을 기대합니다.
        float samples[MAX_IPC_CHANNELS][MAX_SAMPLES_PER_BUFFER];
    };

    // 공유 메모리 최상단 메타데이터 헤더
    struct SharedMemoryHeader
    {
        uint32_t magic;              // IPC_MAGIC
        uint32_t version;            // IPC_VERSION
        uint32_t sampleRate;         // 예: 44100, 48000, 96000
        uint32_t activeChannels;     // 송출 중인 활성 채널 수 (보통 2)
        std::atomic<uint32_t> writeIndex{0}; // Producer가 다음에 쓸 슬롯 인덱스
        std::atomic<uint32_t> readIndex{0};  // Consumer가 다음에 읽을 슬롯 인덱스
        std::atomic<bool> isStreaming{false}; // 드라이버 시작/중지 상태
    };

    // 공유 메모리 전체 레이아웃
    struct SharedMemoryLayout
    {
        SharedMemoryHeader header;
        AudioSlot slots[RING_BUFFER_SLOTS];
    };
    #pragma pack(pop)

    // 공유 메모리 전체 크기 계산
    inline constexpr size_t SHARED_MEMORY_SIZE = sizeof(SharedMemoryLayout);
}
