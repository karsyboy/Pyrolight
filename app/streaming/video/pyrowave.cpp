#include "pyrowave.h"
#include <PyroWave.h>
#include "streaming/session.h"
#include "streaming/streamutils.h"
#include <QtEndian>
#include <cstring>
#include <cstdio>

PyroWaveVideoDecoder::~PyroWaveVideoDecoder()
{
    if (m_OverlayAttached) Session::get()->getOverlayManager().setOverlayRenderer(nullptr);
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Stopping = true;
    }
    m_FrameReady.notify_one();
    if (m_RenderThread.joinable()) m_RenderThread.join();
    if (m_Decoder) pyrowave_decoder_destroy(m_Decoder);
    if (m_Device) pyrowave_device_destroy(m_Device);
    if (m_Renderer && m_Renderer->getVulkan()) {
        auto vk = m_Renderer->getVulkan();
        // The timeline remains alive until libplacebo has finished all its waits.
        pl_gpu_finish(vk->gpu);
        for (auto& plane : m_Planes) pl_tex_destroy(vk->gpu, &plane);
        if (m_Timeline) {
            auto destroy = reinterpret_cast<PFN_vkDestroySemaphore>(vk->get_proc_addr(vk->instance, "vkDestroySemaphore"));
            destroy(vk->device, m_Timeline, nullptr);
        }
    }
}

void PyroWaveVideoDecoder::lockQueue(void* opaque)
{
    auto self = static_cast<PyroWaveVideoDecoder*>(opaque);
    auto vk = self->m_Renderer->getVulkan();
    vk->lock_queue(vk, self->m_Queue.familyIndex, 0);
}

void PyroWaveVideoDecoder::unlockQueue(void* opaque)
{
    auto self = static_cast<PyroWaveVideoDecoder*>(opaque);
    auto vk = self->m_Renderer->getVulkan();
    vk->unlock_queue(vk, self->m_Queue.familyIndex, 0);
}

void PyroWaveVideoDecoder::collectPerformanceStat(void* opaque, const char* message)
{
    auto self = static_cast<PyroWaveVideoDecoder*>(opaque);
    double milliseconds;
    if (std::sscanf(message, "Dequant: %lf ms per frame", &milliseconds) == 1) {
        self->m_GpuDequantMs = milliseconds;
    }
    else if (std::sscanf(message, "iDWT: %lf ms per frame", &milliseconds) == 1 ||
             std::sscanf(message, "iDWT fragment: %lf ms per frame", &milliseconds) == 1) {
        self->m_GpuIdwtMs = milliseconds;
    }
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "PyroWave GPU: %s", message);
}

bool PyroWaveVideoDecoder::borrowDevice()
{
    auto vk = m_Renderer->getVulkan();
    auto inst = m_Renderer->getVulkanInstance();
    auto getPhysicalDeviceProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
        vk->get_proc_addr(vk->instance, "vkGetPhysicalDeviceProperties"));
    if (!getPhysicalDeviceProperties) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "PyroWave could not query the Vulkan physical device");
        return false;
    }
    VkPhysicalDeviceProperties properties {};
    getPhysicalDeviceProperties(vk->phys_device, &properties);
    if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "PyroWave refuses software Vulkan device '%s'; a hardware Vulkan GPU is required",
                     properties.deviceName);
        return false;
    }
    m_AppInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    m_AppInfo.apiVersion = inst->api_version;
    m_InstanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    m_InstanceInfo.pApplicationInfo = &m_AppInfo;
    m_InstanceInfo.enabledExtensionCount = inst->num_extensions;
    m_InstanceInfo.ppEnabledExtensionNames = inst->extensions;
    m_QueueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    m_QueueInfo.queueFamilyIndex = vk->queue_graphics.index;
    m_QueueInfo.queueCount = 1;
    m_QueueInfo.pQueuePriorities = &m_Priority;
    m_DeviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    m_DeviceInfo.pNext = vk->features;
    m_DeviceInfo.queueCreateInfoCount = 1;
    m_DeviceInfo.pQueueCreateInfos = &m_QueueInfo;
    m_DeviceInfo.enabledExtensionCount = vk->num_extensions;
    m_DeviceInfo.ppEnabledExtensionNames = vk->extensions;
    m_Queue.familyIndex = vk->queue_graphics.index;
    auto getQueue = reinterpret_cast<PFN_vkGetDeviceQueue>(vk->get_proc_addr(vk->instance, "vkGetDeviceQueue"));
    getQueue(vk->device, m_Queue.familyIndex, 0, &m_Queue.queue);
    pyrowave_device_create_info info {};
    info.GetInstanceProcAddr = vk->get_proc_addr;
    info.instance = vk->instance;
    info.physical_device = vk->phys_device;
    info.device = vk->device;
    info.instance_create_info = &m_InstanceInfo;
    info.device_create_info = &m_DeviceInfo;
    info.queue_info = &m_Queue;
    info.queue_info_count = 1;
    info.queue_lock_callback = lockQueue;
    info.queue_unlock_callback = unlockQueue;
    info.userdata = this;
    if (pyrowave_create_device(&info, &m_Device) != PYROWAVE_SUCCESS) return false;
    if (pyrowave_device_set_queue_type(m_Device, VK_QUEUE_GRAPHICS_BIT) != PYROWAVE_SUCCESS) return false;
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "PyroWave hardware Vulkan decode device: %s (vendor 0x%04x, device 0x%04x)",
                properties.deviceName, properties.vendorID, properties.deviceID);
    return true;
}

bool PyroWaveVideoDecoder::createPlanes()
{
    auto vk = m_Renderer->getVulkan();
    VkSemaphoreTypeCreateInfo type {};
    type.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    VkSemaphoreCreateInfo info {};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    info.pNext = &type;
    auto create = reinterpret_cast<PFN_vkCreateSemaphore>(vk->get_proc_addr(vk->instance, "vkCreateSemaphore"));
    if (create(vk->device, &info, nullptr, &m_Timeline) != VK_SUCCESS) return false;
    for (int p = 0; p < 3; p++) {
        const int shift = p && !(m_Format & VIDEO_FORMAT_PYROWAVE_444) ? 1 : 0;
        pl_tex_params params {};
        params.w = m_Width >> shift;
        params.h = m_Height >> shift;
        params.format = pl_find_named_fmt(vk->gpu, "r16");
        params.sampleable = true;
        params.storable = !m_FragmentPath;
        params.renderable = m_FragmentPath;
        if (!params.format || !(m_Planes[p] = pl_tex_create(vk->gpu, &params))) return false;
        auto& view = m_Buffers.planes[p];
        view.image = pl_vulkan_unwrap(vk->gpu, m_Planes[p], &view.image_format, nullptr);
        view.width = params.w;
        view.height = params.h;
        view.view_format = view.image_format;
        view.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        view.swizzle = VK_COMPONENT_SWIZZLE_IDENTITY;
        view.layout = VK_IMAGE_LAYOUT_GENERAL;
        if (!view.image) return false;
    }
    return true;
}

bool PyroWaveVideoDecoder::initialize(PDECODER_PARAMETERS params)
{
    uint32_t major, minor, patch;
    pyrowave_get_api_version(&major, &minor, &patch);
    if (major != 0 || minor != 7 || params->width <= 0 || params->height <= 0 ||
        params->width > 8192 || params->height > 8192 || params->vds == StreamingPreferences::VDS_FORCE_SOFTWARE) return false;
    m_Width = params->width;
    m_Height = params->height;
    m_Format = params->videoFormat;
    m_Window = params->window;
    m_Renderer = std::make_unique<PlVkRenderer>();
    if (!m_Renderer->initialize(params) || !borrowDevice()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "PyroWave cannot share the Vulkan rendering device");
        return false;
    }
    pyrowave_decoder_create_info info {};
    info.device = m_Device;
    info.width = m_Width;
    info.height = m_Height;
    info.chroma = m_Format & VIDEO_FORMAT_PYROWAVE_444 ? PYROWAVE_CHROMA_SUBSAMPLING_444 : PYROWAVE_CHROMA_SUBSAMPLING_420;
    bool fragmentOverrideSet = false;
    const int fragmentOverride = qEnvironmentVariableIntValue("PYROWAVE_FRAGMENT_PATH", &fragmentOverrideSet);
    m_FragmentPath = fragmentOverrideSet ? fragmentOverride != 0 :
        pyrowave_decoder_device_prefers_fragment_path(m_Device);
    info.fragment_path = m_FragmentPath;
    if (pyrowave_decoder_create(&info, &m_Decoder) != PYROWAVE_SUCCESS || !createPlanes()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "PyroWave decoder/plane creation failed");
        return false;
    }
    if (!params->testOnly) {
        Session::get()->getOverlayManager().setOverlayRenderer(m_Renderer.get());
        m_OverlayAttached = true;
        try {
            m_Threaded = true;
            m_RenderThread = std::thread(&PyroWaveVideoDecoder::renderLoop, this);
        }
        catch (const std::system_error& error) {
            m_Threaded = false;
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to start PyroWave render thread: %s", error.what());
            return false;
        }
    }
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Native PyroWave Vulkan decoder: %dx%d@%d, %s, %s, %s path; display %d Hz, V-sync %s, frame pacing %s",
                m_Width, m_Height, params->frameRate,
                m_Format & VIDEO_FORMAT_PYROWAVE_444 ? "4:4:4" : "4:2:0",
                m_Format & VIDEO_FORMAT_PYROWAVE_HDR ? "HDR" : "SDR",
                m_FragmentPath ? "fragment" : "compute",
                StreamUtils::getDisplayRefreshRate(m_Window),
                params->enableVsync ? "on" : "off",
                params->enableFramePacing ? "on" : "off");
    return true;
}

void PyroWaveVideoDecoder::addVideoStats(const VIDEO_STATS& src, VIDEO_STATS& dst, uint64_t now)
{
    dst.receivedFrames += src.receivedFrames;
    dst.decodedFrames += src.decodedFrames;
    dst.renderedFrames += src.renderedFrames;
    dst.totalFrames += src.totalFrames;
    dst.networkDroppedFrames += src.networkDroppedFrames;
    dst.pacerDroppedFrames += src.pacerDroppedFrames;
    dst.totalReassemblyTimeUs += src.totalReassemblyTimeUs;
    dst.totalDecodeTimeUs += src.totalDecodeTimeUs;
    dst.totalPacerTimeUs += src.totalPacerTimeUs;
    dst.totalRenderTimeUs += src.totalRenderTimeUs;

    if (dst.minHostProcessingLatency == 0 ||
        (src.minHostProcessingLatency != 0 && src.minHostProcessingLatency < dst.minHostProcessingLatency)) {
        dst.minHostProcessingLatency = src.minHostProcessingLatency;
    }
    dst.maxHostProcessingLatency = qMax(dst.maxHostProcessingLatency, src.maxHostProcessingLatency);
    dst.totalHostProcessingLatency += src.totalHostProcessingLatency;
    dst.framesWithHostProcessingLatency += src.framesWithHostProcessingLatency;

    if (!LiGetEstimatedRttInfo(&dst.lastRtt, &dst.lastRttVariance)) {
        dst.lastRtt = dst.lastRttVariance = 0;
    }
    if (dst.measurementStartUs == 0 ||
        (src.measurementStartUs != 0 && src.measurementStartUs < dst.measurementStartUs)) {
        dst.measurementStartUs = src.measurementStartUs;
    }
    if (dst.measurementStartUs != 0 && now > dst.measurementStartUs) {
        const double seconds = double(now - dst.measurementStartUs) / 1000000.0;
        dst.totalFps = double(dst.totalFrames) / seconds;
        dst.receivedFps = double(dst.receivedFrames) / seconds;
        dst.decodedFps = double(dst.decodedFrames) / seconds;
        dst.renderedFps = double(dst.renderedFrames) / seconds;
    }
}

void PyroWaveVideoDecoder::updatePerformanceOverlay(const VIDEO_STATS& stats)
{
    auto& overlay = Session::get()->getOverlayManager();
    if (!overlay.isOverlayEnabled(Overlay::OverlayDebug) || stats.receivedFrames == 0) {
        return;
    }

    char text[1024];
    const char* chroma = m_Format & VIDEO_FORMAT_PYROWAVE_444 ? " 4:4:4" : " 4:2:0";
    const char* dynamicRange = m_Format & VIDEO_FORMAT_PYROWAVE_HDR ? " HDR" : " SDR";
    char rtt[64];
    if (stats.lastRtt != 0) {
        std::snprintf(rtt, sizeof(rtt), "%u ms (variance: %u ms)", stats.lastRtt, stats.lastRttVariance);
    }
    else {
        std::snprintf(rtt, sizeof(rtt), "N/A");
    }

    int used = std::snprintf(text, sizeof(text),
        "Video stream: %dx%d %.2f FPS (Codec: PyroWave%s%s)\n"
        "Incoming frame rate from network: %.2f FPS\n"
        "Decoding frame rate: %.2f FPS\n"
        "Rendering frame rate: %.2f FPS\n",
        m_Width, m_Height, stats.totalFps, dynamicRange, chroma,
        stats.receivedFps, stats.decodedFps, stats.renderedFps);
    if (used < 0 || used >= int(sizeof(text))) {
        return;
    }

    if (stats.framesWithHostProcessingLatency != 0) {
        const int written = std::snprintf(text + used, sizeof(text) - size_t(used),
            "Host processing latency min/max/average: %.1f/%.1f/%.1f ms\n",
            float(stats.minHostProcessingLatency) / 10.0f,
            float(stats.maxHostProcessingLatency) / 10.0f,
            float(stats.totalHostProcessingLatency) / 10.0f / stats.framesWithHostProcessingLatency);
        if (written < 0 || written >= int(sizeof(text)) - used) {
            return;
        }
        used += written;
    }

    if (m_GpuDequantMs > 0.0 || m_GpuIdwtMs > 0.0) {
        const int gpuWritten = std::snprintf(text + used, sizeof(text) - size_t(used),
            "PyroWave GPU dequant/iDWT: %.2f/%.2f ms\n",
            m_GpuDequantMs, m_GpuIdwtMs);
        if (gpuWritten < 0 || gpuWritten >= int(sizeof(text)) - used) {
            return;
        }
        used += gpuWritten;
    }

    if (stats.renderedFrames != 0) {
        const double networkDropPercent = stats.totalFrames != 0 ?
            double(stats.networkDroppedFrames) * 100.0 / stats.totalFrames : 0.0;
        const double clientDropPercent = stats.receivedFrames != 0 ?
            double(stats.pacerDroppedFrames) * 100.0 / stats.receivedFrames : 0.0;
        const int written = std::snprintf(text + used, sizeof(text) - size_t(used),
            "Frames dropped by your network connection: %.2f%%\n"
            "Frames dropped by the client frame queue: %.2f%%\n"
            "Average network latency: %s\n"
            "Average decode submission time: %.2f ms\n"
            "Average frame queue delay: %.2f ms\n"
            "Average presentation wait + render submission time: %.2f ms\n",
            networkDropPercent, clientDropPercent, rtt,
            double(stats.totalDecodeTimeUs) / 1000.0 / stats.decodedFrames,
            double(stats.totalPacerTimeUs) / 1000.0 / stats.renderedFrames,
            double(stats.totalRenderTimeUs) / 1000.0 / stats.renderedFrames);
        if (written < 0 || written >= int(sizeof(text)) - used) {
            return;
        }
        used += written;
    }

    overlay.updateOverlayText(Overlay::OverlayDebug, text);
}

int PyroWaveVideoDecoder::submitDecodeUnit(PDECODE_UNIT du)
{
    if (du->fullLength < 8 || du->fullLength > int(PYROWAVE_MAX_FRAME_BYTES)) return DR_OK;
    std::vector<uint32_t> frame;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        frame.swap(m_Spare);
    }
    frame.resize((du->fullLength + 3) / 4);
    size_t offset = 0;
    for (auto entry = du->bufferList; entry; entry = entry->next) {
        if (entry->length <= 0 || size_t(entry->length) > size_t(du->fullLength) - offset) return DR_OK;
        memcpy(reinterpret_cast<uint8_t*>(frame.data()) + offset, entry->data, entry->length);
        offset += entry->length;
    }
    if (offset != size_t(du->fullLength)) return DR_OK;
    const uint64_t now = LiGetMicroseconds();
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_ActiveVideoStats.measurementStartUs == 0) {
            m_ActiveVideoStats.measurementStartUs = now;
            m_LastFrameNumber = du->frameNumber;
        }
        else {
            if (du->frameNumber > m_LastFrameNumber + 1) {
                const uint32_t dropped = uint32_t(du->frameNumber - (m_LastFrameNumber + 1));
                m_ActiveVideoStats.networkDroppedFrames += dropped;
                m_ActiveVideoStats.totalFrames += dropped;
            }
            m_LastFrameNumber = du->frameNumber;
        }

        if (now > m_ActiveVideoStats.measurementStartUs + 1000000) {
            m_PendingOverlayStats = {};
            addVideoStats(m_LastVideoStats, m_PendingOverlayStats, now);
            addVideoStats(m_ActiveVideoStats, m_PendingOverlayStats, now);
            addVideoStats(m_ActiveVideoStats, m_GlobalVideoStats, now);
            m_LastVideoStats = m_ActiveVideoStats;
            m_ActiveVideoStats = {};
            m_ActiveVideoStats.measurementStartUs = now;
            m_OverlayRefreshPending = true;
        }

        if (du->frameHostProcessingLatency != 0) {
            if (m_ActiveVideoStats.minHostProcessingLatency == 0) {
                m_ActiveVideoStats.minHostProcessingLatency = du->frameHostProcessingLatency;
            }
            else {
                m_ActiveVideoStats.minHostProcessingLatency =
                    qMin(m_ActiveVideoStats.minHostProcessingLatency, du->frameHostProcessingLatency);
            }
            m_ActiveVideoStats.maxHostProcessingLatency =
                qMax(m_ActiveVideoStats.maxHostProcessingLatency, du->frameHostProcessingLatency);
            m_ActiveVideoStats.totalHostProcessingLatency += du->frameHostProcessingLatency;
            m_ActiveVideoStats.framesWithHostProcessingLatency++;
        }
        m_ActiveVideoStats.receivedFrames++;
        m_ActiveVideoStats.totalFrames++;
        m_ActiveVideoStats.totalReassemblyTimeUs += du->enqueueTimeUs - du->receiveTimeUs;
        if (!m_Pending.empty()) {
            // The mailbox keeps the newest complete frame to minimize latency.
            m_ActiveVideoStats.pacerDroppedFrames++;
        }

        if (!m_Pending.empty()) {
            m_Pending.swap(frame);
            if (frame.capacity() > m_Spare.capacity()) m_Spare.swap(frame);
        }
        else {
            m_Pending = std::move(frame);
        }
        m_PendingSize = offset;
        m_EnqueueTime = now;
        if (!m_Threaded && !m_EventQueued) {
            SDL_Event event {};
            event.type = SDL_USEREVENT;
            event.user.code = SDL_CODE_FRAME_READY;
            event.user.windowID = SDL_GetWindowID(m_Window);
            m_EventQueued = SDL_PushEvent(&event) == 1;
        }
    }
    if (m_Threaded) m_FrameReady.notify_one();
    return DR_OK;
}

void PyroWaveVideoDecoder::releasePlanes(uint64_t value)
{
    auto vk = m_Renderer->getVulkan();
    for (auto plane : m_Planes) {
        pl_vulkan_release_params params {};
        params.tex = plane;
        params.layout = VK_IMAGE_LAYOUT_GENERAL;
        params.qf = VK_QUEUE_FAMILY_IGNORED;
        params.semaphore = {m_Timeline, value};
        pl_vulkan_release_ex(vk->gpu, &params);
    }
}

bool PyroWaveVideoDecoder::decodeFrame(const std::vector<uint32_t>& bytes, size_t size,
                                       uint64_t* decodeTimeUs, uint64_t* renderTimeUs,
                                       bool rendererReady)
{
    if (size < 8 || size > PYROWAVE_MAX_FRAME_BYTES || size > bytes.size() * sizeof(uint32_t)) return false;
    // Sequence header metadata is used directly; no assumptions about bit depth.
    const uint32_t a = qFromLittleEndian(bytes[0]), b = qFromLittleEndian(bytes[1]);
    if (!(a >> 31) || ((b >> 24) & 3) || int((a & 0x3fff) + 1) != m_Width ||
        int(((a >> 14) & 0x3fff) + 1) != m_Height ||
        bool(b & (1u << 26)) != bool(m_Format & VIDEO_FORMAT_PYROWAVE_444) ||
        bool(b & (1u << 28)) != bool(m_Format & VIDEO_FORMAT_PYROWAVE_HDR)) return false;
    pyrowave_decoder_clear(m_Decoder);
    if (pyrowave_decoder_push_packet(m_Decoder, bytes.data(), size) != PYROWAVE_SUCCESS ||
        !pyrowave_decoder_decode_is_ready(m_Decoder, false)) return false;
    auto vk = m_Renderer->getVulkan();
    for (int p = 0; p < 3; p++) {
        pl_vulkan_hold_params params {};
        params.tex = m_Planes[p];
        params.layout = VK_IMAGE_LAYOUT_GENERAL;
        params.qf = VK_QUEUE_FAMILY_IGNORED;
        params.semaphore = {m_Timeline, ++m_Value};
        if (!pl_vulkan_hold_ex(vk->gpu, &params)) {
            // Return every successfully held plane; device reset handles failure.
            for (int i = 0; i < p; i++) {
                pl_vulkan_release_params release {};
                release.tex = m_Planes[i]; release.layout = VK_IMAGE_LAYOUT_GENERAL;
                release.qf = VK_QUEUE_FAMILY_IGNORED; release.semaphore = {m_Timeline, m_Value - 1};
                pl_vulkan_release_ex(vk->gpu, &release);
            }
            return false;
        }
    }
    pyrowave_gpu_sync_operation acquire {}, release {};
    acquire.sync = {m_Timeline, m_Value};
    release.sync = {m_Timeline, ++m_Value};
    const auto decodeStart = LiGetMicroseconds();
    auto result = pyrowave_decoder_decode_gpu_buffer(m_Decoder, &acquire, &release, &m_Buffers);
    if (result != PYROWAVE_SUCCESS) {
        // No valid signal will follow a failed decode. Finish before dropping ownership.
        pl_gpu_finish(vk->gpu);
        releasePlanes(0);
        return false;
    }
    const auto decodeEnd = LiGetMicroseconds();
    releasePlanes(m_Value);
    pl_frame frame {};
    frame.num_planes = 3;
    frame.crop = {0, 0, float(m_Width), float(m_Height)};
    frame.repr.bits.sample_depth = frame.repr.bits.color_depth = 16;
    frame.repr.sys = b & (1u << 29) ? PL_COLOR_SYSTEM_BT_2020_NC : PL_COLOR_SYSTEM_BT_709;
    frame.repr.levels = b & (1u << 30) ? PL_COLOR_LEVELS_LIMITED : PL_COLOR_LEVELS_FULL;
    frame.color.primaries = b & (1u << 27) ? PL_COLOR_PRIM_BT_2020 : PL_COLOR_PRIM_BT_709;
    frame.color.transfer = b & (1u << 28) ? PL_COLOR_TRC_PQ : PL_COLOR_TRC_BT_1886;
    if ((m_Format & VIDEO_FORMAT_PYROWAVE_HDR) && m_OverlayAttached) {
        SS_HDR_METADATA metadata {};
        if (LiGetHdrMetadata(&metadata)) {
            if (metadata.maxDisplayLuminance) {
                frame.color.hdr.max_luma = metadata.maxDisplayLuminance;
                frame.color.hdr.min_luma = metadata.minDisplayLuminance ? metadata.minDisplayLuminance / 10000.0f : PL_COLOR_HDR_BLACK;
            }
            frame.color.hdr.max_cll = metadata.maxContentLightLevel;
            frame.color.hdr.max_fall = metadata.maxFrameAverageLightLevel;
            if (metadata.displayPrimaries[0].x) {
                frame.color.hdr.prim.red = {metadata.displayPrimaries[0].x / 50000.0f, metadata.displayPrimaries[0].y / 50000.0f};
                frame.color.hdr.prim.green = {metadata.displayPrimaries[1].x / 50000.0f, metadata.displayPrimaries[1].y / 50000.0f};
                frame.color.hdr.prim.blue = {metadata.displayPrimaries[2].x / 50000.0f, metadata.displayPrimaries[2].y / 50000.0f};
                frame.color.hdr.prim.white = {metadata.whitePoint.x / 50000.0f, metadata.whitePoint.y / 50000.0f};
            }
        }
    }
    for (int p = 0; p < 3; p++) {
        frame.planes[p].texture = m_Planes[p];
        frame.planes[p].components = 1;
        frame.planes[p].component_mapping[0] = p;
    }
    pl_frame_set_chroma_location(&frame, b >> 31 ? PL_CHROMA_LEFT : PL_CHROMA_CENTER);
    const auto renderStart = LiGetMicroseconds();
    if (!rendererReady) m_Renderer->waitToRender();
    m_Renderer->renderPlaceboFrame(frame);
    const auto renderEnd = LiGetMicroseconds();
    if (decodeTimeUs) *decodeTimeUs = decodeEnd - decodeStart;
    if (renderTimeUs) *renderTimeUs = renderEnd - renderStart;
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "PyroWave decode+render submission: %llu us (%zu bytes)",
                 static_cast<unsigned long long>(renderEnd - decodeStart), size);
    if (decodeStart - m_LastStatsTime >= 1000000) {
        pyrowave_device_report_performance_stats(m_Device, collectPerformanceStat, this, true);
        m_LastStatsTime = decodeStart;
        if ((m_GpuDequantMs > 0.0 || m_GpuIdwtMs > 0.0) &&
            decodeStart - m_LastGpuStatsLogTime >= 5000000) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "PyroWave GPU decode (%s path): dequant %.2f ms, iDWT %.2f ms",
                        m_FragmentPath ? "fragment" : "compute",
                        m_GpuDequantMs, m_GpuIdwtMs);
            m_LastGpuStatsLogTime = decodeStart;
        }
    }
    return true;
}

void PyroWaveVideoDecoder::recycleFrame(std::vector<uint32_t>& frame)
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    frame.clear();
    if (frame.capacity() > m_Spare.capacity()) m_Spare.swap(frame);
}

void PyroWaveVideoDecoder::renderPendingFrame(bool rendererReady)
{
    std::vector<uint32_t> frame;
    VIDEO_STATS overlayStats {};
    bool refreshOverlay = false;
    size_t size;
    uint64_t enqueueTime;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        frame = std::move(m_Pending);
        size = m_PendingSize;
        enqueueTime = m_EnqueueTime;
        m_EventQueued = false;
    }
    if (frame.empty()) return;
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "PyroWave packet completion to decode: %llu us",
                 static_cast<unsigned long long>(LiGetMicroseconds() - enqueueTime));
    const uint64_t processingStart = LiGetMicroseconds();
    uint64_t decodeTimeUs = 0;
    uint64_t renderTimeUs = 0;
    if (!decodeFrame(frame, size, &decodeTimeUs, &renderTimeUs, rendererReady)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Rejected incomplete/invalid PyroWave frame or GPU operation failed");
        // The synchronous probe path may already own a swapchain frame.
        if (rendererReady) m_Renderer->cleanupRenderContext();
        if (pl_gpu_is_failed(m_Renderer->getVulkan()->gpu)) {
            SDL_Event event {}; event.type = SDL_RENDER_DEVICE_RESET; SDL_PushEvent(&event);
        }
    }
    else {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_ActiveVideoStats.decodedFrames++;
        m_ActiveVideoStats.renderedFrames++;
        m_ActiveVideoStats.totalDecodeTimeUs += decodeTimeUs;
        m_ActiveVideoStats.totalPacerTimeUs += processingStart - enqueueTime;
        m_ActiveVideoStats.totalRenderTimeUs += renderTimeUs;
        if (m_OverlayRefreshPending) {
            overlayStats = m_PendingOverlayStats;
            m_OverlayRefreshPending = false;
            refreshOverlay = true;
        }
    }
    recycleFrame(frame);
    if (refreshOverlay) updatePerformanceOverlay(overlayStats);
}

void PyroWaveVideoDecoder::renderLoop()
{
    if (SDL_SetThreadPriority(SDL_THREAD_PRIORITY_HIGH) < 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Unable to set PyroWave render thread priority: %s", SDL_GetError());
    }
    while (true) {
        {
            std::unique_lock<std::mutex> lock(m_Mutex);
            m_FrameReady.wait(lock, [this] { return m_Stopping || !m_Pending.empty(); });
            if (m_Stopping) break;
        }

        // Submit decode before waiting for presentation capacity. The timeline
        // dependency still orders this decode after the preceding render, but
        // the GPU can execute it while this thread waits for the next swapchain
        // frame. Waiting first creates a decode -> present -> decode bubble that
        // is especially costly at 120 Hz and above.
        renderPendingFrame(false);
    }
    m_Renderer->cleanupRenderContext();
}

void PyroWaveVideoDecoder::renderFrameOnMainThread()
{
    // Production uses the dedicated Vulkan render thread. The synchronous path
    // remains for renderer probing and the GPU smoke test.
    if (m_Threaded) return;
    m_Renderer->waitToRender();
    renderPendingFrame(true);
}

bool PyroWaveVideoDecoder::notifyWindowChanged(PWINDOW_STATE_CHANGE_INFO info)
{
    return m_Renderer->notifyWindowChanged(info);
}
