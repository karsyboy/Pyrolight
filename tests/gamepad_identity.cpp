#ifdef NDEBUG
#undef NDEBUG
#endif
#include "streaming/input/gamepadidentity.h"
#include <SDL.h>
#include <cassert>

int main()
{
    using namespace GamepadIdentity;
    assert(isDualSenseEdge(0x054c, 0x0df2));
    assert(!isDualSenseEdge(0x054c, 0x0ce6));
    assert(!isDualSenseEdge(0x045e, 0x0df2));
    assert(!isDualSenseEdge(0, 0));
    assert(EdgeButtons == 0x000f0000);
    assert(ButtonMap[SDL_CONTROLLER_BUTTON_PADDLE1] == PADDLE1_FLAG);
    assert(ButtonMap[SDL_CONTROLLER_BUTTON_PADDLE2] == PADDLE2_FLAG);
    assert(ButtonMap[SDL_CONTROLLER_BUTTON_PADDLE3] == PADDLE3_FLAG);
    assert(ButtonMap[SDL_CONTROLLER_BUTTON_PADDLE4] == PADDLE4_FLAG);
    const int ordinary[] = { A_FLAG, B_FLAG, X_FLAG, Y_FLAG, BACK_FLAG,
        SPECIAL_FLAG, PLAY_FLAG, LS_CLK_FLAG, RS_CLK_FLAG, LB_FLAG, RB_FLAG,
        UP_FLAG, DOWN_FLAG, LEFT_FLAG, RIGHT_FLAG, MISC_FLAG };
    for (unsigned i = 0; i < sizeof(ordinary) / sizeof(ordinary[0]); ++i) {
        assert(ButtonMap[i] == ordinary[i]);
    }
    assert(ButtonMap[SDL_CONTROLLER_BUTTON_TOUCHPAD] == TOUCHPAD_FLAG);
    assert(DualSenseEdgeCapability == 0x200);

    struct Identity { uint16_t vendor, product, caps; uint8_t family; };
    const Identity identities[] = {
        {0x045e, 0x02dd, 0, LI_CTYPE_XBOX},
        {0x045e, 0x02e3, LI_CCAP_XBOX_ELITE, LI_CTYPE_XBOX},
        {0x045e, 0x0b00, 0x2400, LI_CTYPE_XBOX},
        {0x045e, 0x0b05, 0x2400, LI_CTYPE_XBOX},
        {0x045e, 0x0b22, 0x2400, LI_CTYPE_XBOX},
        {0x28de, 0x1101, 0x800, LI_CTYPE_STEAM},
        {0x28de, 0x1102, 0x800, LI_CTYPE_STEAM},
        {0x28de, 0x1105, 0x800, LI_CTYPE_STEAM},
        {0x28de, 0x1106, 0x800, LI_CTYPE_STEAM},
        {0x28de, 0x1142, 0x800, LI_CTYPE_STEAM},
        {0x28de, 0x1205, 0x1000, LI_CTYPE_STEAM},
        {0x054c, 0x0ce6, 0, LI_CTYPE_PS},
        {0x054c, 0x0df2, 0x200, LI_CTYPE_PS},
        {0x28de, 0xffff, 0, LI_CTYPE_UNKNOWN},
        {0x28de, 0x11ff, 0, LI_CTYPE_UNKNOWN},
        {0x28de, 0x1304, 0, LI_CTYPE_UNKNOWN}, // future Valve family, not classic
        {0x2dc8, 0x6003, 0, LI_CTYPE_UNKNOWN}, // unrelated paddles
        {0x045e, 0x0b02, 0, LI_CTYPE_UNKNOWN}, // GIP keyboard, not Elite
        {0, 0, 0, LI_CTYPE_UNKNOWN},
    };
    for (const auto& identity : identities) {
        assert(subtype(identity.vendor, identity.product) == identity.caps);
        assert(subtypeFamily(identity.caps, identity.family) == identity.family);
        if (identity.caps) assert(subtypeFamily(identity.caps, LI_CTYPE_UNKNOWN) == identity.family);
    }

#if SDL_VERSION_ATLEAST(2, 24, 0)
    assert(SDL_Init(SDL_INIT_GAMECONTROLLER) == 0);
    // Raw fixtures follow native SDL2 HIDAPI mappings. User/DB mappings can
    // differ; the stream always consumes normalized SDL buttons.
    struct Fixture { uint16_t vendor, product; int raw[4]; int paddles; };
    const Fixture fixtures[] = {
        {0x054c, 0x0df2, {20, 19, 18, 17}, 4},
        {0x045e, 0x02e3, {15, 17, 16, 18}, 4},
        {0x045e, 0x0b00, {15, 17, 16, 18}, 4},
        {0x28de, 0x1102, {16, 15, 0, 0}, 2},
        {0x28de, 0x1205, {16, 17, 18, 19}, 4},
    };
    for (const auto& fixture : fixtures) {
        SDL_VirtualJoystickDesc desc = {};
        desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
        desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
        desc.nbuttons = 21;
        desc.vendor_id = fixture.vendor;
        desc.product_id = fixture.product;
        desc.name = "Native normalization test";
        int device = SDL_JoystickAttachVirtualEx(&desc);
        assert(device >= 0);
        SDL_Joystick* joystick = SDL_JoystickOpen(device);
        assert(joystick);
        char guid[33], mapping[256];
        SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(joystick), guid, sizeof(guid));
        SDL_snprintf(mapping, sizeof(mapping), "%s,Native normalization test,a:b0,paddle1:b%d,paddle2:b%d,",
                     guid, fixture.raw[0], fixture.raw[1]);
        if (fixture.paddles == 4) {
            char extras[64];
            SDL_snprintf(extras, sizeof(extras), "paddle3:b%d,paddle4:b%d,", fixture.raw[2], fixture.raw[3]);
            SDL_strlcat(mapping, extras, sizeof(mapping));
        }
        assert(SDL_GameControllerAddMapping(mapping) >= 0);
        SDL_GameController* controller = SDL_GameControllerOpen(device);
        assert(controller);
        assert(subtype(SDL_GameControllerGetVendor(controller), SDL_GameControllerGetProduct(controller)) ==
               subtype(fixture.vendor, fixture.product));
        uint32_t supported = 0;
        for (unsigned i = 0; i < sizeof(ButtonMap) / sizeof(ButtonMap[0]); ++i) {
            if (SDL_GameControllerHasButton(controller, (SDL_GameControllerButton)i)) supported |= ButtonMap[i];
        }
        const unsigned combinations = 1u << fixture.paddles;
        const uint32_t expected = (combinations - 1) << 16;
        assert((supported & EdgeButtons) == expected);
        SDL_Event event;
        while (SDL_PollEvent(&event)) {}
        uint32_t streamed = 0;
        for (unsigned state = 0; state <= combinations; ++state) {
            const unsigned pressed = state == combinations ? 0 : state;
            for (int i = 0; i < fixture.paddles; ++i) {
                assert(SDL_JoystickSetVirtualButton(joystick, fixture.raw[i], (pressed >> i) & 1) == 0);
            }
            SDL_JoystickUpdate();
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_CONTROLLERBUTTONDOWN) streamed |= ButtonMap[event.cbutton.button];
                if (event.type == SDL_CONTROLLERBUTTONUP) streamed &= ~ButtonMap[event.cbutton.button];
            }
            assert(streamed == (pressed << 16));
            assert(SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_A) == 0);
            for (int i = 0; i < fixture.paddles; ++i) {
                assert(SDL_GameControllerGetButton(controller, (SDL_GameControllerButton)(SDL_CONTROLLER_BUTTON_PADDLE1 + i)) == ((pressed >> i) & 1));
            }
        }
        SDL_GameControllerClose(controller);
        SDL_JoystickClose(joystick);
        assert(SDL_JoystickDetachVirtual(device) == 0);
    }
    SDL_Quit();
#endif
}
