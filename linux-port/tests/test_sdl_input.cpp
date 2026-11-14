// Virtual SDL devices drive the production input pump, including hotplug and feedback.
#include <SDL3/SDL.h>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include "../tools/dcp_run/sdl_input.h"
#include "hle/input.h"
namespace dcp::hle {
std::unique_ptr<InputSource> source;
void set_input_source(std::unique_ptr<InputSource> s) { source = std::move(s); }
std::unique_ptr<InputSource> take_input_source() { return std::move(source); }
}
namespace dcp::render { uint32_t fairy_cursor(float*, float*) { return 0; } }
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s (%s)\n", __LINE__, #x, SDL_GetError()); return 1; } } while (0)
struct Feedback { int calls = 0; Uint16 low = 0, high = 0; bool stale = false; };
int main() {
    SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE);
    SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_HIDAPI, "0", SDL_HINT_OVERRIDE);
    SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_MFI, "0", SDL_HINT_OVERRIDE);
    SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_IOKIT, "0", SDL_HINT_OVERRIDE);
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    CHECK(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD));
    SDL_Window* win = SDL_CreateWindow("input test", 640, 360, SDL_WINDOW_HIDDEN);
    CHECK(win);
    Feedback feedback;
    SDL_VirtualJoystickTouchpadDesc touch{}; touch.nfingers = 2;
    SDL_VirtualJoystickDesc desc{}; SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD; desc.name = "OpenCrown virtual regression";
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT; desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
    desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
    desc.ntouchpads = 1; desc.touchpads = &touch;
    desc.userdata = &feedback;
    desc.Rumble = [](void* p, Uint16 l, Uint16 h) {
        auto& f = *static_cast<Feedback*>(p); ++f.calls; f.low = l; f.high = h;
        f.stale |= l == 65535 || h == 65535;
        return true;
    };
    auto first = SDL_AttachVirtualJoystick(&desc); CHECK(first);
    auto* held = SDL_OpenGamepad(first); CHECK(held); // separately owned reference
    auto* joy = SDL_GetGamepadJoystick(held);
    std::string dir = "sdl-input-test-" + std::to_string(SDL_GetPerformanceCounter());
    CHECK(std::filesystem::create_directory(dir));
    auto* pump = install_sdl_input(dir);
    auto tick = [&] {
        SDL_UpdateJoysticks(); SDL_Event e;
        while (SDL_PollEvent(&e)) pump->handle_event(e);
        pump->update(win);
    };
    tick();
    auto read = [&](int slot = 0) { return dcp::hle::source->read(slot, 0); };
    CHECK(!dcp::hle::source->present(1));
    const std::pair<SDL_GamepadButton, uint32_t> buttons[] = {
        {SDL_GAMEPAD_BUTTON_SOUTH,0x4000}, {SDL_GAMEPAD_BUTTON_EAST,0x2000},
        {SDL_GAMEPAD_BUTTON_WEST,0x8000}, {SDL_GAMEPAD_BUTTON_NORTH,0x1000},
        {SDL_GAMEPAD_BUTTON_START,8}, {SDL_GAMEPAD_BUTTON_BACK,0x100000},
        {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,0x400}, {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,0x800},
        {SDL_GAMEPAD_BUTTON_LEFT_STICK,2}, {SDL_GAMEPAD_BUTTON_RIGHT_STICK,4},
        {SDL_GAMEPAD_BUTTON_DPAD_UP,0x10}, {SDL_GAMEPAD_BUTTON_DPAD_DOWN,0x40},
        {SDL_GAMEPAD_BUTTON_DPAD_LEFT,0x80}, {SDL_GAMEPAD_BUTTON_DPAD_RIGHT,0x20},
        {SDL_GAMEPAD_BUTTON_TOUCHPAD,0x100000}};
    for (auto [button, bit] : buttons) {
        CHECK(SDL_SetJoystickVirtualButton(joy, button, true)); tick(); CHECK(read().buttons & bit);
        CHECK(!pump->kbm_active());
        CHECK(SDL_SetJoystickVirtualButton(joy, button, false)); tick(); CHECK(!(read().buttons & bit));
    }
    CHECK(SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_LEFTX, -32768));
    CHECK(SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_RIGHTY, 32767));
    CHECK(SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 32767)); tick();
    CHECK(read().lx == 0 && read().ry == 255 && read().l2 == 255 && (read().buttons & 0x100));
    CHECK(SDL_SetJoystickVirtualTouchpad(joy, 0, 0, true, .25f, .75f, 1.f)); tick();
    CHECK(read().touch_num == 1 && read().tx[0] == 479 && read().ty[0] == 706);
    dcp::hle::source->set_vibration(0, 100, 200); tick();
    CHECK(feedback.calls && feedback.low == 25700 && feedback.high == 51400);
    SDL_Event key{}; key.type = SDL_EVENT_KEY_DOWN; pump->handle_event(key);
    CHECK(pump->kbm_active());
    auto second = SDL_AttachVirtualJoystick(&desc); CHECK(second); tick();
    CHECK(dcp::hle::source->present(1));
    CHECK(SDL_SetJoystickVirtualButton(joy, SDL_GAMEPAD_BUTTON_SOUTH, true)); tick(); CHECK(read().buttons & 0x4000);
    CHECK(SDL_DetachVirtualJoystick(first)); tick();
    CHECK(!dcp::hle::source->present(1) && read().buttons == 0 && read().lx == 128 && read().touch_num == 0);
    // Reconnect the same model with a fresh instance: neither held input nor pending rumble leaks to it.
    dcp::hle::source->set_vibration(1, 255, 255);
    auto third = SDL_AttachVirtualJoystick(&desc); CHECK(third); tick();
    CHECK(dcp::hle::source->present(1) && read(1).buttons == 0 && !feedback.stale);
    auto* third_pad = SDL_GetGamepadFromID(third); CHECK(third_pad);
    CHECK(SDL_SetJoystickVirtualButton(SDL_GetGamepadJoystick(third_pad), SDL_GAMEPAD_BUTTON_NORTH, true)); tick();
    CHECK(read(1).buttons & 0x1000);
    // Process replacement in one rescan, with the same total device count.
    CHECK(SDL_DetachVirtualJoystick(third));
    auto fourth = SDL_AttachVirtualJoystick(&desc); CHECK(fourth); tick();
    CHECK(dcp::hle::source->present(1) && read(1).buttons == 0 && read(1).touch_num == 0);
    // Dropping the source releases only its own SDL references.
    dcp::hle::source.reset();
    CHECK(SDL_GetGamepadFromID(first) == held);
    SDL_CloseGamepad(held);
    CHECK(SDL_GetGamepadFromID(second) == nullptr && SDL_GetGamepadFromID(fourth) == nullptr);
    CHECK(SDL_DetachVirtualJoystick(second)); CHECK(SDL_DetachVirtualJoystick(fourth));
    std::string ini = std::string(dir) + "/input.ini";
    SDL_RemovePath(ini.c_str()); SDL_RemovePath(dir.c_str());
    SDL_DestroyWindow(win); SDL_Quit();
    std::puts("SDL input: buttons, sticks, triggers, touch, prompts, rumble, disconnect/reconnect and handle ownership passed");
}
