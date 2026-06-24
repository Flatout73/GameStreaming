/*
 *  sound_command_sender.h
 *
 *  Server-side sound-command queue + streamer. The game thread enqueues
 *  SoundCommands (play/stop/volume); a dedicated worker thread drains the
 *  queue and streams them to the client over IPv6 UDP via UDPSender6. The
 *  server plays no audio of its own — the client renders every sound.
 *
 *  Reliability over plain UDP:
 *   - Each command is sent a few times back-to-back (cheap, dedup'd by seq).
 *   - "Persistent" state (the current music loop + master volume) is re-sent
 *     on a ~1.5 s heartbeat so a client that joins late, or misses the
 *     original datagram, converges. Re-sends reuse the original seq, so a
 *     client that already applied the command ignores it.
 *
 *  Header-only and #included into game.cpp (same pattern as event_receiver.cpp
 *  / frame_encoder.cpp). Relies on UDPSender6 from udp_sender6.cpp.
 */

#pragma once

#include "net_sound_command.h"
#include "udp_sender6.h"

#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <mutex>
#include <thread>

class SoundCommandSender {
public:
  SoundCommandSender() = default;
  ~SoundCommandSender() { stop(); }

  // Resolve the destination and start the worker thread. Address/port can be
  // overridden via SOUND_ADDR / SOUND_PORT (defaults match the client).
  void start(const char *address = "::1", int port = 50002) {
    if (m_running.load())
      return;
    const char *envAddr = std::getenv("SOUND_ADDR");
    const char *envPort = std::getenv("SOUND_PORT");
    const char *addr = envAddr ? envAddr : address;
    int p = envPort ? std::atoi(envPort) : port;

    m_sender.init(addr, p);
    if (m_sender.sock < 0) {
      std::cerr << "SoundCommandSender: init failed (socket not open); "
                   "client audio disabled"
                << std::endl;
      return;
    }
    m_running.store(true);
    m_worker = std::thread([this] { workerLoop(); });
    std::cout << "SoundCommandSender: streaming sound commands to [" << addr
              << "]:" << p << std::endl;
  }

  void stop() {
    {
      std::lock_guard<std::mutex> lk(m_mx);
      if (!m_running.exchange(false))
        return;
    }
    m_cv.notify_all();
    if (m_worker.joinable())
      m_worker.join();

    // Best-effort: silence the client before we disappear, so a looping music
    // track doesn't outlive the server. Sent a few times for reliability.
    if (m_sender.sock >= 0) {
      netsound::SoundCommand c{};
      c.action = netsound::ACTION_STOP_ALL;
      c.seq = ++m_seq;
      for (int i = 0; i < 3; i++)
        sendCmd(c);
    }
    m_sender.closeSock();
  }

  // ---- game-thread API (thread-safe) ----

  // Start a sound. For SOUND_MUSIC with loops != 0 the command is remembered
  // as persistent state so late-joining clients still get the music.
  void play(netsound::SoundId id, int loops, int volume) {
    netsound::SoundCommand c{};
    c.action = netsound::ACTION_PLAY;
    c.soundId = id;
    c.loops = loops;
    c.volume = volume;
    c.seq = ++m_seq;

    if (id == netsound::SOUND_MUSIC && loops != 0) {
      std::lock_guard<std::mutex> lk(m_stateMx);
      m_music = c;
      m_hasMusic = true;
    }
    enqueue(c, /*repeats=*/3);
  }

  // Stop a single sound (used to stop the looping music on death).
  void stopSound(netsound::SoundId id) {
    netsound::SoundCommand c{};
    c.action = netsound::ACTION_STOP;
    c.soundId = id;
    c.seq = ++m_seq;

    if (id == netsound::SOUND_MUSIC) {
      // Persist the STOP as the current music state so the heartbeat keeps
      // re-asserting it. Otherwise a lost STOP datagram would leave the client
      // looping the music forever, and a client that joins after death would
      // never be told to stop.
      std::lock_guard<std::mutex> lk(m_stateMx);
      m_music = c;
      m_hasMusic = true;
    }
    enqueue(c, /*repeats=*/3);
  }

  // Update the client's master volume (0..128). Remembered + heartbeated.
  void setMasterVolume(int volume) {
    netsound::SoundCommand c{};
    c.action = netsound::ACTION_SET_MASTER_VOLUME;
    c.volume = volume;
    c.seq = ++m_seq;
    {
      std::lock_guard<std::mutex> lk(m_stateMx);
      m_master = c;
      m_hasMaster = true;
    }
    enqueue(c, /*repeats=*/3);
  }

private:
  void enqueue(const netsound::SoundCommand &c, int repeats) {
    {
      std::lock_guard<std::mutex> lk(m_mx);
      for (int i = 0; i < repeats; i++)
        m_queue.push_back(c);
    }
    m_cv.notify_one();
  }

  void sendCmd(const netsound::SoundCommand &c) {
    m_sender.send(reinterpret_cast<const char *>(&c),
                  static_cast<int>(sizeof(c)));
  }

  void workerLoop() {
    using namespace std::chrono;
    const auto heartbeat = milliseconds(1500);
    auto nextBeat = steady_clock::now() + heartbeat;

    while (m_running.load()) {
      std::deque<netsound::SoundCommand> batch;
      {
        std::unique_lock<std::mutex> lk(m_mx);
        m_cv.wait_until(lk, nextBeat,
                        [this] { return !m_queue.empty() || !m_running.load(); });
        batch.swap(m_queue);
      }

      for (const auto &c : batch) {
        if (!m_running.load())
          return;
        sendCmd(c);
      }

      if (steady_clock::now() >= nextBeat) {
        std::lock_guard<std::mutex> lk(m_stateMx);
        if (m_hasMaster)
          sendCmd(m_master);
        if (m_hasMusic)
          sendCmd(m_music);
        nextBeat = steady_clock::now() + heartbeat;
      }
    }
  }

  UDPSender6 m_sender;
  std::thread m_worker;
  std::atomic<bool> m_running{false};
  std::atomic<uint32_t> m_seq{0};

  std::mutex m_mx;
  std::condition_variable m_cv;
  std::deque<netsound::SoundCommand> m_queue;

  // Persistent state re-asserted on the heartbeat. m_music holds the current
  // music command — either the PLAY (looping) or the STOP — so both transitions
  // converge on a late-joining or packet-losing client. m_hasMusic just means
  // "a music state exists to assert".
  std::mutex m_stateMx;
  netsound::SoundCommand m_music{};
  netsound::SoundCommand m_master{};
  bool m_hasMusic{false};
  bool m_hasMaster{false};
};
