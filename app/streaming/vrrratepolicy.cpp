#include "vrrratepolicy.h"

#include <map>

namespace {

bool isUsableRefreshRate(int refreshHz)
{
    // SDL reports zero for an unknown refresh rate.
    return refreshHz > 1;
}

void addChoice(std::map<int, VrrFpsChoiceKind>& choices, int fps, VrrFpsChoiceKind kind)
{
    // The first role for a rate wins: fixed and native rates are inserted first.
    if (fps > 0) {
        choices.emplace(fps, kind);
    }
}

}

int VrrRatePolicy::vrrRateForRefresh(int refreshHz)
{
    if (!isUsableRefreshRate(refreshHz)) {
        return 0;
    }
    const long long numerator = static_cast<long long>(refreshHz) * (3600LL - refreshHz);
    return numerator > 0 ? static_cast<int>(numerator / 3600LL) : 0;
}

int VrrRatePolicy::lowLatencyRateForRefresh(int refreshHz)
{
    return isUsableRefreshRate(refreshHz) ? (refreshHz / 6) * 5 : 0;
}

bool VrrRatePolicy::hasAdaptiveHeadroom(int streamRateHz, int displayRefreshHz)
{
    return streamRateHz > 0 && displayRefreshHz > 0 && streamRateHz <= displayRefreshHz;
}

std::vector<VrrFpsChoice> VrrRatePolicy::buildChoices(const std::vector<int>& refreshRates,
                                                      int savedFps,
                                                      bool vrrEnabled)
{
    std::map<int, VrrFpsChoiceKind> choices;
    addChoice(choices, 30, VrrFpsChoiceKind::Fixed);
    addChoice(choices, 60, VrrFpsChoiceKind::Fixed);
    for (const int refreshHz : refreshRates) {
        if (isUsableRefreshRate(refreshHz)) {
            addChoice(choices, refreshHz, VrrFpsChoiceKind::Fixed);
        }
    }
    if (vrrEnabled) {
        for (const int refreshHz : refreshRates) {
            addChoice(choices, vrrRateForRefresh(refreshHz), VrrFpsChoiceKind::Vrr);
            addChoice(choices, lowLatencyRateForRefresh(refreshHz), VrrFpsChoiceKind::LowLatencyVrr);
        }
    }
    if (savedFps > 0) {
        addChoice(choices, savedFps, VrrFpsChoiceKind::Custom);
    }

    std::vector<VrrFpsChoice> result;
    result.reserve(choices.size());
    for (const auto& choice : choices) {
        result.push_back({choice.first, choice.second});
    }
    return result;
}
