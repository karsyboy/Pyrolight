#pragma once

#include <stdint.h>
#include <Limelight.h>

namespace GamepadIdentity {
constexpr int ButtonMap[] = {
    A_FLAG, B_FLAG, X_FLAG, Y_FLAG,
    BACK_FLAG, SPECIAL_FLAG, PLAY_FLAG,
    LS_CLK_FLAG, RS_CLK_FLAG,
    LB_FLAG, RB_FLAG,
    UP_FLAG, DOWN_FLAG, LEFT_FLAG, RIGHT_FLAG,
    MISC_FLAG,
    // SDL2 canonical Edge order: right rear, left rear, right Fn, left Fn.
    PADDLE1_FLAG, PADDLE2_FLAG, PADDLE3_FLAG, PADDLE4_FLAG,
    TOUCHPAD_FLAG,
};

constexpr uint16_t SonyVendorId = 0x054c;
constexpr uint16_t DualSenseEdgeProductId = 0x0df2;
constexpr uint32_t EdgeButtons = PADDLE1_FLAG | PADDLE2_FLAG | PADDLE3_FLAG | PADDLE4_FLAG;

constexpr bool isDualSenseEdge(uint16_t vendorId, uint16_t productId)
{
    return vendorId == SonyVendorId && productId == DualSenseEdgeProductId;
}
}
