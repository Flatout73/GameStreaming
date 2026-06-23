#pragma once

#include <cstdint>

namespace netsound {

enum SoundAction : uint32_t {
  ACTION_PLAY = 1,              // start `soundId` (loops/volume apply)
  ACTION_STOP = 2,              // stop `soundId` (used for the music loop)
  ACTION_STOP_ALL = 3,          // stop every sound
  ACTION_SET_MASTER_VOLUME = 4, // set the client's master volume (`volume`)
};

enum SoundId : uint32_t {
  SOUND_NONE = 0,
  SOUND_MUSIC = 1,     // looping background music
  SOUND_BELL = 2,      // fruit eaten
  SOUND_EXPLOSION = 3, // crash death
  SOUND_GAMEOVER = 4,  // game over jingle
};

struct SoundCommand {
  uint32_t action;  // SoundAction
  uint32_t soundId; // SoundId
  int32_t loops;    // -1 = loop forever, 0 = play once, N = repeat N more times
  int32_t volume;   // 0..128 volume scale, mapped to 0..1 on the client
  uint32_t seq;     // monotonically increasing; the client dedups resends by it
};

static_assert(sizeof(SoundCommand) == 20,
              "SoundCommand wire layout must be 20 bytes");

} // namespace netsound
