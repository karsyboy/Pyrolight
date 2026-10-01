#include "../app/backend/pyrowavebandwidth.h"
#include "../moonlight-common-c/moonlight-common-c/src/PyroWave.h"
#include <cassert>
#include <climits>
int main() {
    using namespace PyroWaveBandwidth;
    assert(throughputKbps(ProbeBytes - 1, 1000000) == 0);
    assert(throughputKbps(ProbeBytes, 0) == 0);
    assert(throughputKbps(ProbeBytes, 268435456) == 1000000);
    assert(ceilingKbps({941000, 960000, 955000}, 1000, 1000) == 752800);
    assert(ceilingKbps({1000000, 1000000, 1000000}, 1000, 1000) == 800000);
    assert(ceilingKbps({400000, 600000, 500000}, 1000, 1000) == 320000);
    assert(ceilingKbps({941000, 960000, 955000}, 0, 0) == 752800);
    assert(ceilingKbps({941000, 960000, 955000}, 100, 0) == 80000);
    assert(ceilingKbps({0, 1000, 1000}, 0, 0) == 0);
    assert(LiVideoReceiveBufferSize(1392, 1) == 11534336);
    assert(LiVideoReceiveBufferSize(1024, 1) == 8519680);
    assert(LiVideoReceiveBufferSize(1392, 0) == 2883584);
    assert(LiVideoReceiveBufferSize(INT_MAX, 1) == 0);
    assert(LiVideoReceiveBufferSize(0, 1) == 0);
    assert(LiPyroWaveReceiveLimitStatus(4LL*1024*1024, 1392) == PYROWAVE_RECEIVE_LIMIT_INADEQUATE);
    assert(LiPyroWaveReceiveLimitStatus(32LL*1024*1024, 1392) == PYROWAVE_RECEIVE_LIMIT_ADEQUATE);
    assert(LiPyroWaveReceiveLimitStatus(11534336, 1392) == PYROWAVE_RECEIVE_LIMIT_ADEQUATE);
    assert(LiPyroWaveReceiveLimitStatus(0, 1392) == PYROWAVE_RECEIVE_LIMIT_UNKNOWN);
    assert(LiPyroWaveReceiveLimitStatus(-1, 1392) == PYROWAVE_RECEIVE_LIMIT_UNKNOWN);
}
