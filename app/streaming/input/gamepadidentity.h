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

// Subtypes are defined by common-c, the protocol source of truth.
constexpr uint16_t DualSenseEdgeCapability = LI_CCAP_DUALSENSE_EDGE;
static_assert(DualSenseEdgeCapability == 0x0200, "Edge wire capability");
static_assert(LI_CCAP_XBOX_ELITE == 0x0400 && LI_CCAP_STEAM_CONTROLLER == 0x0800 &&
              LI_CCAP_STEAM_DECK == 0x1000 && LI_CCAP_XBOX_ELITE_SERIES_2 == 0x2000,
              "Native controller wire capabilities");

constexpr bool isDualSenseEdge(uint16_t vendorId, uint16_t productId)
{
    return vendorId == SonyVendorId && productId == DualSenseEdgeProductId;
}

// SDL usb_ids.h/controller_list.h: no paddle-count or device-name heuristics.
constexpr uint16_t subtype(uint16_t vendor, uint16_t product)
{
    return isDualSenseEdge(vendor, product) ? LI_CCAP_DUALSENSE_EDGE :
        vendor == 0x045e ?
            (product == 0x02e3 ? LI_CCAP_XBOX_ELITE :
             product == 0x0b00 || product == 0x0b05 || product == 0x0b22 ?
                 LI_CCAP_XBOX_ELITE | LI_CCAP_XBOX_ELITE_SERIES_2 : 0) :
        vendor == 0x28de ?
            (product == 0x1205 ? LI_CCAP_STEAM_DECK :
             product == 0x1101 || product == 0x1102 || product == 0x1105 ||
             product == 0x1106 || product == 0x1142 ? LI_CCAP_STEAM_CONTROLLER : 0) : 0;
}

constexpr uint8_t subtypeFamily(uint16_t model, uint8_t fallback)
{
    return model & LI_CCAP_DUALSENSE_EDGE ? LI_CTYPE_PS :
        model & LI_CCAP_XBOX_ELITE ? LI_CTYPE_XBOX :
        model & (LI_CCAP_STEAM_CONTROLLER | LI_CCAP_STEAM_DECK) ? LI_CTYPE_STEAM : fallback;
}
}
