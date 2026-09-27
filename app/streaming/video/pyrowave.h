#pragma once
#include "decoder.h"
#include "ffmpeg-renderers/plvk.h"
#include <pyrowave.h>
#include <memory>
#include <mutex>
#include <vector>

// All GPU operations run on the main render thread. submitDecodeUnit copies a
// bounded complete encoded frame into a single-slot mailbox; newer frames replace it.
class PyroWaveVideoDecoder final : public IVideoDecoder {
public:
    ~PyroWaveVideoDecoder() override;
    bool initialize(PDECODER_PARAMETERS params) override;
    bool isHardwareAccelerated() override { return true; }
    bool isAlwaysFullScreen() override { return false; }
    bool isHdrSupported() override { return true; }
    int getDecoderCapabilities() override { return 0; }
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
    bool borrowDevice();
    bool createPlanes();
    bool decodeFrame(const std::vector<uint32_t>& bytes, size_t size,
                     uint64_t* decodeTimeUs = nullptr, uint64_t* renderTimeUs = nullptr);
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
    SDL_Window* m_Window = nullptr;
    std::mutex m_Mutex;
    std::vector<uint32_t> m_Pending;
    size_t m_PendingSize = 0;
    bool m_EventQueued = false;
    bool m_OverlayAttached = false;
    uint64_t m_EnqueueTime = 0;
    uint64_t m_LastStatsTime = 0;
    VIDEO_STATS m_ActiveVideoStats = {};
    VIDEO_STATS m_LastVideoStats = {};
    VIDEO_STATS m_GlobalVideoStats = {};
    int m_LastFrameNumber = 0;
};
