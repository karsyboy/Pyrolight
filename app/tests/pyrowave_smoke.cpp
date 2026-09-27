#include "streaming/video/pyrowave.h"
#include <PyroWave.h>
#include <QCoreApplication>
#include <SDL_vulkan.h>
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <fstream>
std::atomic<int> g_AsyncLoggingEnabled {0};
#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "Failed: %s at %d\n", #x, __LINE__); return false; } } while (0)
class PyroWaveDecoderSmokeTest {
public:
    static bool run(SDL_Window *window, int format, int extent) {
        PyroWaveVideoDecoder decoder;
        DECODER_PARAMETERS params {};
        params.window = window; params.videoFormat = format;
        params.width = params.height = extent; params.frameRate = 60;
        params.testOnly = true;
        REQUIRE(decoder.initialize(&params));
        if (const auto directory = std::getenv("SUNSHINE_TEST_PYROWAVE_OUTPUT")) {
            for (int index = 0; index < 16; ++index) {
                const auto filename = std::string(directory) + "/" + std::to_string(extent) + "-" +
                    std::to_string(!!(format & VIDEO_FORMAT_PYROWAVE_HDR)) + "-" +
                    std::to_string(!!(format & VIDEO_FORMAT_PYROWAVE_444)) + "-" + std::to_string(index) + ".pyro";
                std::ifstream stream(filename, std::ios::binary | std::ios::ate);
                REQUIRE(stream.good());
                const auto size = stream.tellg(); REQUIRE(size >= 8 && size <= PYROWAVE_MAX_FRAME_BYTES);
                std::vector<uint32_t> data((size_t(size) + 3) / 4);
                stream.seekg(0); stream.read(reinterpret_cast<char *>(data.data()), size);
                REQUIRE(stream.good()); REQUIRE(decoder.decodeFrame(data, size_t(size)));
            }
            printf("Rendered Sunshine DMA-BUF output: %dx%d format 0x%x\n", extent, extent, format);
            return true;
        }
        pyrowave_device device = nullptr;
        REQUIRE(pyrowave_create_default_device(&device) == PYROWAVE_SUCCESS);
        pyrowave_encoder encoder = nullptr;
        pyrowave_encoder_create_info info {};
        info.device = device; info.width = info.height = extent;
        const bool c444 = format & VIDEO_FORMAT_PYROWAVE_444;
        info.chroma = c444 ? PYROWAVE_CHROMA_SUBSAMPLING_444 : PYROWAVE_CHROMA_SUBSAMPLING_420;
        REQUIRE(pyrowave_encoder_create(&info, &encoder) == PYROWAVE_SUCCESS);
        pyrowave_color_metadata color {};
        color.color_primaries = color.transfer_function = color.ycbcr_transform = !!(format & VIDEO_FORMAT_PYROWAVE_HDR);
        REQUIRE(pyrowave_encoder_set_color_metadata(encoder, &color) == PYROWAVE_SUCCESS);
        std::vector<uint8_t> y(extent * extent), c(extent * extent, 128);
        pyrowave_cpu_buffer input {};
        input.width = input.height = extent;
        input.format = c444 ? PYROWAVE_CPU_BUFFER_FORMAT_YUV444P : PYROWAVE_CPU_BUFFER_FORMAT_YUV420P;
        input.data[0] = y.data(); input.data[1] = input.data[2] = c.data();
        for (int p = 0; p < 3; p++) {
            input.row_stride_in_bytes[p] = p && !c444 ? extent / 2 : extent;
            input.plane_size_in_bytes[p] = p && !c444 ? c.size() / 4 : y.size();
        }
        pyrowave_rate_control rate {65536};
        std::vector<uint32_t> frame(65536 / 4);
        for (int i = 0; i < 24; i++) {
            for (size_t n = 0; n < y.size(); n++) y[n] = uint8_t(n + i * 5);
            REQUIRE(pyrowave_encoder_encode_cpu_synchronous(encoder, &input, &rate) == PYROWAVE_SUCCESS);
            pyrowave_packet packet {}; size_t count;
            REQUIRE(pyrowave_encoder_packetize(encoder, &packet, 65536, &count, frame.data(), frame.size() * 4) == PYROWAVE_SUCCESS);
            REQUIRE(count == 1 && packet.offset == 0);
            if (i == 0) {
                auto invalid = frame; invalid[0] = 0;
                REQUIRE(!decoder.decodeFrame(invalid, packet.size));
                REQUIRE(!decoder.decodeFrame(frame, 8)); // Truncated complete frame.
            }
            REQUIRE(decoder.decodeFrame(frame, packet.size));
            // Simulate dropped complete frames and sequence wrap.
            if (i % 3 == 0) continue;
            LENTRY entry {}; entry.data = reinterpret_cast<char*>(frame.data()); entry.length = packet.size;
            DECODE_UNIT unit {}; unit.fullLength = packet.size; unit.bufferList = &entry;
            REQUIRE(decoder.submitDecodeUnit(&unit) == DR_OK);
            decoder.renderFrameOnMainThread();
            SDL_Event event; while (SDL_PollEvent(&event)) {}
        }
        pyrowave_encoder_destroy(encoder); pyrowave_device_destroy(device);
        printf("Rendered %dx%d %s %s with shared Vulkan device\n", extent, extent, c444 ? "444" : "420", color.transfer_function ? "PQ/2020" : "709");
        return true;
    }
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    SDL_LogSetAllPriority(SDL_LOG_PRIORITY_INFO);
    if (SDL_Init(SDL_INIT_VIDEO) || SDL_Vulkan_LoadLibrary(nullptr)) return 2;
    SDL_Window *window = SDL_CreateWindow("PyroWave renderer test", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 256, 256, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
    if (!window) { fprintf(stderr, "%s\n", SDL_GetError()); return 2; }
    for (int extent : {64, 128}) for (int chroma : {VIDEO_FORMAT_PYROWAVE, VIDEO_FORMAT_PYROWAVE_444}) for (int hdr : {0, VIDEO_FORMAT_PYROWAVE_HDR}) {
        if (!PyroWaveDecoderSmokeTest::run(window, chroma | hdr, extent)) return 1;
    }
    SDL_DestroyWindow(window); SDL_Quit();
    puts("Shared Vulkan renderer startup/frame/drop/resize/reconnect tests passed");
}
