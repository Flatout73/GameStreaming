/*
 *  net_input_event.h
 *
 *  Wire format for input events forwarded from the streaming client back to
 *  the server. The client (SwiftSDL) captures SDL3 events from the user and
 *  sends them here; the server re-injects them with SDL_PushEvent.
 *
 *  Layout: 12 x 4-byte fields = 48 bytes, no padding. Must match the Swift
 *  `NetInputEvent` struct byte-for-byte (same machine, little-endian).
 */

#pragma once

#include <cstdint>

namespace netinput {

enum EventKind : uint32_t {
  KIND_KEY_DOWN = 1,
  KIND_KEY_UP = 2,
  KIND_MOUSE_MOTION = 3,
  KIND_MOUSE_BUTTON_DOWN = 4,
  KIND_MOUSE_BUTTON_UP = 5,
  KIND_MOUSE_WHEEL = 6,
};

struct NetInputEvent {
  uint32_t kind;     // EventKind
  uint32_t scancode; // SDL_Scancode raw value (key events)
  uint32_t keycode;  // SDL_Keycode raw value (key events)
  uint32_t mod;      // SDL_Keymod (key events)
  uint32_t button;   // mouse button index (1=left, 2=middle, 3=right)
  uint32_t repeat;   // 0/1 (key events)
  float x;           // mouse position x (motion / button)
  float y;           // mouse position y (motion / button)
  float xrel;        // relative motion x (motion)
  float yrel;        // relative motion y (motion)
  float wheel_x;     // wheel delta x
  float wheel_y;     // wheel delta y
};

} // namespace netinput
