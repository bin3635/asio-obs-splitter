#include <obs-module.h>
#include <util/platform.h>
#include "ipc_receiver.h"
#include <memory>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("asio-obs-splitter", "en-US")

struct AsioSourceContext
{
    obs_source_t* source{nullptr};
    std::unique_ptr<IpcReceiver> receiver;
    uint64_t nextTimestamp{0};
};

static const char* asio_source_get_name(void* unused)
{
    UNUSED_PARAMETER(unused);
    return "ASIO Proxy Capture";
}

static void* asio_source_create(obs_data_t* settings, obs_source_t* source)
{
    UNUSED_PARAMETER(settings);
    auto* ctx = new AsioSourceContext();
    ctx->source = source;
    ctx->receiver = std::make_unique<IpcReceiver>();

    // IPC 워커의 데이터를 OBS 오디오 버스로 넘긴다. 콜백은 워커 스레드에서 실행된다.
    ctx->receiver->start([ctx](const float* const* channelData, uint32_t channels, uint32_t sampleCount, uint32_t sampleRate) {
        if (!channelData || channels == 0 || channels > 8 || sampleCount == 0 || sampleRate == 0)
        {
            return;
        }

        obs_source_audio audio{};
        for (uint32_t ch = 0; ch < channels; ++ch)
        {
            audio.data[ch] = reinterpret_cast<const uint8_t*>(channelData[ch]);
        }
        audio.frames = sampleCount;
        audio.speakers = (channels == 1) ? SPEAKERS_MONO : SPEAKERS_STEREO;
        audio.format = AUDIO_FORMAT_FLOAT_PLANAR;
        audio.samples_per_sec = sampleRate;
        if (ctx->nextTimestamp == 0)
        {
            ctx->nextTimestamp = os_gettime_ns();
        }
        audio.timestamp = ctx->nextTimestamp;
        ctx->nextTimestamp += (static_cast<uint64_t>(sampleCount) * 1000000000ULL) / sampleRate;

        obs_source_output_audio(ctx->source, &audio);
    });

    return ctx;
}

static void asio_source_destroy(void* data)
{
    auto* ctx = static_cast<AsioSourceContext*>(data);
    if (ctx)
    {
        if (ctx->receiver)
        {
            ctx->receiver->stop();
        }
        delete ctx;
    }
}

static obs_source_info asio_source_info = {
    .id = "asio_proxy_source",
    .type = OBS_SOURCE_TYPE_INPUT,
    .output_flags = OBS_SOURCE_AUDIO | OBS_SOURCE_DO_NOT_DUPLICATE,
    .get_name = asio_source_get_name,
    .create = asio_source_create,
    .destroy = asio_source_destroy,
};

bool obs_module_load(void)
{
    obs_register_source(&asio_source_info);
    return true;
}

void obs_module_unload(void)
{
}
