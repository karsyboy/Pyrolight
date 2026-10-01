#include "streaming/video/pyrowaveframe.h"
#include "streaming/video/pyrowaveframing.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <string>

int main(int argc, char** argv)
{
    assert(argc == 2);
    for (bool hdr : {false, true}) for (bool c444 : {false, true}) {
        const auto mode = std::string(hdr ? "hdr" : "sdr") + (c444 ? "444" : "420");
        for (const auto& dialect : {"native", "records"}) {
            std::ifstream file(std::string(argv[1]) + "/" + dialect + "/" + mode + ".bin", std::ios::binary);
            assert(file.good());
            std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), {});
            std::vector<uint8_t> seen;
            if (std::string(dialect) == "native") {
                const auto original = data;
                assert(pyroWaveValidateFrame(data.data(), data.size(), 256, 144, c444, hdr, seen));
                assert(data == original); // The sole wire-v1 path preserves codec bytes.
            } else {
                // Nonary's scaled encoder leaves color metadata at default.
                if (hdr) assert(!pyroWaveValidateFrame(data.data(), data.size(), 256, 144, c444, hdr, seen));
                PyroWaveFraming::Frame parsed;
                std::string error;
                assert(PyroWaveFraming::parse(data.data(), data.size(), {256, 144, c444}, parsed, error));
                assert(!parsed.partial && parsed.coarseLevelIntact);
                size_t size = data.size();
                assert(pyroWaveUnpackRecords(data.data(), size, hdr));
                assert(pyroWaveValidateFrame(data.data(), size, 256, 144, c444, hdr, seen));
            }
        }
    }
}
