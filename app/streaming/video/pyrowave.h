#pragma once
#include "decoder.h"
#include "ffmpeg-renderers/plvk.h"
#include <pyrowave/pyrowave.h>
#include <memory>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

constexpr int pyroWaveVisiblePlaneDimension(int visibleDimension, bool chroma420Plane)
{
    return visibleDimension >> (chroma420Plane ? 1 : 0);
}

// PyroWave's aligned wavelet extent must never leak into renderer textures.
static_assert(pyroWaveVisiblePlaneDimension(1920, false) == 1920);
static_assert(pyroWaveVisiblePlaneDimension(1080, false) == 1080);
static_assert(pyroWaveVisiblePlaneDimension(2560, false) == 2560);
static_assert(pyroWaveVisiblePlaneDimension(1440, false) == 1440);
static_assert(pyroWaveVisiblePlaneDimension(3440, false) == 3440);
static_assert(pyroWaveVisiblePlaneDimension(3840, false) == 3840);
static_assert(pyroWaveVisiblePlaneDimension(2160, false) == 2160);
static_assert(((3440 + 31) & ~31) == 3456 && pyroWaveVisiblePlaneDimension(3440, false) != 3456);
static_assert(((2160 + 31) & ~31) == 2176 && pyroWaveVisiblePlaneDimension(2160, false) != 2176);

// GPU operations run on a dedicated render thread. submitDecodeUnit is a
// non-blocking producer for a single-slot encoded-frame mailbox; newer frames
// replace it so presentation latency cannot accumulate.
class PyroWaveVideoDecoder final : public IVideoDecoder {
public:
    ~PyroWaveVideoDecoder() override;
    bool initialize(PDECODER_PARAMETERS params) override;
    bool isHardwareAccelerated() override { return true; }
    bool isAlwaysFullScreen() override { return false; }
    bool isHdrSupported() override { return true; }
    int getDecoderCapabilities() override { return CAPABILITY_DIRECT_SUBMIT; }
    int getDecoderColorspace() override { return COLORSPACE_REC_709; }
    int getDecoderColorRange() override { return COLOR_RANGE_FULL; }
    QSize getDecoderMaxResolution() override { return QSize(8192, 8192); }
    int submitDecodeUnit(PDECODE_UNIT du) override;
    void renderFrameOnMainThread() override;
    void setHdrMode(bool) override {}
    bool notifyWindowChanged(PWINDOW_STATE_CHANGE_INFO info) override;
private:
    friend class PyroWaveDecoderSmokeTest;
    static void lockQueue(void* opaque);
    static void unlockQueue(void* opaque);
    static void collectPerformanceStat(void* opaque, const char* message);
    bool borrowDevice();
    bool createPlanes();
    bool decodeFrame(const std::vector<uint32_t>& bytes, size_t size,
                     uint64_t* decodeTimeUs = nullptr, uint64_t* renderTimeUs = nullptr,
                     bool rendererReady = false);
    void renderPendingFrame(bool rendererReady);
    void renderLoop();
    void recycleFrame(std::vector<uint32_t>& frame);
    void releasePlanes(uint64_t value);
    static void addVideoStats(const VIDEO_STATS& src, VIDEO_STATS& dst, uint64_t now);
    void updatePerformanceOverlay(const VIDEO_STATS& stats);
    std::unique_ptr<PlVkRenderer> m_Renderer;
    pyrowave_device m_Device = nullptr;
    pyrowave_decoder m_Decoder = nullptr;
    VkSemaphore m_Timeline = VK_NULL_HANDLE;
    uint64_t m_Value = 0;
    pl_tex m_Planes[3] = {};
    pyrowave_gpu_buffers m_Buffers = {};
    VkApplicationInfo m_AppInfo = {};
    VkInstanceCreateInfo m_InstanceInfo = {};
    VkDeviceCreateInfo m_DeviceInfo = {};
    VkDeviceQueueCreateInfo m_QueueInfo = {};
    pyrowave_device_create_queue_info m_Queue = {};
    float m_Priority = 1.0f;
    int m_Width = 0, m_Height = 0, m_Format = 0;
    bool m_FragmentPath = false;
    SDL_Window* m_Window = nullptr;
    std::mutex m_Mutex;
    std::condition_variable m_FrameReady;
    std::thread m_RenderThread;
    std::vector<uint8_t> m_BlockSeen;
    std::vector<uint32_t> m_Pending;
    std::vector<uint32_t> m_Spare;
    size_t m_PendingSize = 0;
    bool m_EventQueued = false;
    bool m_Threaded = false;
    bool m_Stopping = false;
    bool m_OverlayAttached = false;
    uint64_t m_EnqueueTime = 0;
    uint64_t m_LastStatsTime = 0;
    uint64_t m_LastGpuStatsLogTime = 0;
    double m_GpuDequantMs = 0.0;
    double m_GpuIdwtMs = 0.0;
    VIDEO_STATS m_ActiveVideoStats = {};
    VIDEO_STATS m_LastVideoStats = {};
    VIDEO_STATS m_PendingOverlayStats = {};
    bool m_OverlayRefreshPending = false;
    int m_LastFrameNumber = 0;
};
