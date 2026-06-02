#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <utility>

extern "C" {
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
}

#include "net_input_event.h"

// EventReceiver – the server-side counterpart to the client's input forwarding.
// It binds an IPv6 UDP socket, receives the SDL events the client captured from
// the user, reconstructs a native SDL_Event and pushes it into this process's
// SDL event queue with SDL_PushEvent. The engine's normal SDL_PollEvent loop
// (WindowSDL::OnPollEvents) then dispatches them, driving the camera, ImGui, etc.
class EventReceiver : public vve::System {
public:
  EventReceiver(vve::Engine &engine, std::string windowName = "")
      : vve::System("EventReceiver", engine),
        m_windowName(std::move(windowName)) {

    const char *envPort = std::getenv("EVENT_PORT");
    m_port = envPort ? std::stoi(envPort) : 50001;

    InitSocket();

    // Drain the network queue at the very start of every frame,
    // before the window system pumps SDL_PollEvent during POLL_EVENTS in the same frame.
    m_engine.RegisterCallbacks(
        {{this, 0, "FRAME_START",
          [this](Message &message) { return OnFrameStart(message); }}});
  }

  ~EventReceiver() {
    if (m_sock >= 0) {
      ::close(m_sock);
      m_sock = -1;
    }
  }

private:
  int m_sock{-1};
  int m_port{50001};
  std::string m_windowName;
  SDL_WindowID m_windowID{0};

  void InitSocket() {
    m_sock = socket(AF_INET6, SOCK_DGRAM, 0);
    if (m_sock < 0) {
      perror("EventReceiver: socket creation failed");
      return;
    }

    int yes = 1;
    setsockopt(m_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in6 addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_addr = in6addr_any;
    addr.sin6_port = htons(static_cast<uint16_t>(m_port));

    if (bind(m_sock, reinterpret_cast<struct sockaddr *>(&addr),
             sizeof(addr)) < 0) {
      perror("EventReceiver: bind failed");
      ::close(m_sock);
      m_sock = -1;
      return;
    }

    // Non-blocking so draining never stalls the render loop.
    int flags = fcntl(m_sock, F_GETFL, 0);
    fcntl(m_sock, F_SETFL, flags | O_NONBLOCK);

    std::cout << "EventReceiver: listening for input events on [::]:" << m_port
              << std::endl;
  }

  // The injected events carry the focused window's id so the ImGui SDL3 backend
  // routes them to the right viewport. Resolved lazily once the window exists.
  SDL_WindowID WindowID() {
    if (m_windowID == 0) {
      auto [handle, wstate, wsdlstate] =
          vve::WindowSDL::GetState(m_registry, std::string{m_windowName});
      if (wsdlstate().m_sdlWindow != nullptr) {
        m_windowID = SDL_GetWindowID(wsdlstate().m_sdlWindow);
      }
    }
    return m_windowID;
  }

  bool OnFrameStart(Message message) {
    if (m_sock < 0)
      return false;

    netinput::NetInputEvent msg;
    while (true) {
      ssize_t n = recvfrom(m_sock, &msg, sizeof(msg), 0, nullptr, nullptr);
      if (n != static_cast<ssize_t>(sizeof(msg)))
        break; // EAGAIN (no more data) or malformed datagram
      InjectEvent(msg);
    }
    return false;
  }

  void InjectEvent(const netinput::NetInputEvent &m) {
    SDL_Event ev;
    SDL_zero(ev);
    SDL_WindowID wid = WindowID();

    switch (m.kind) {
    case netinput::KIND_KEY_DOWN:
    case netinput::KIND_KEY_UP: {
      bool down = (m.kind == netinput::KIND_KEY_DOWN);
      ev.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
      ev.key.windowID = wid;
      ev.key.scancode = static_cast<SDL_Scancode>(m.scancode);
      ev.key.key = static_cast<SDL_Keycode>(m.keycode);
      ev.key.mod = static_cast<SDL_Keymod>(m.mod);
      ev.key.repeat = (m.repeat != 0);
      ev.key.down = down;
      break;
    }
    case netinput::KIND_MOUSE_MOTION: {
      ev.type = SDL_EVENT_MOUSE_MOTION;
      ev.motion.windowID = wid;
      ev.motion.x = m.x;
      ev.motion.y = m.y;
      ev.motion.xrel = m.xrel;
      ev.motion.yrel = m.yrel;
      break;
    }
    case netinput::KIND_MOUSE_BUTTON_DOWN:
    case netinput::KIND_MOUSE_BUTTON_UP: {
      bool down = (m.kind == netinput::KIND_MOUSE_BUTTON_DOWN);
      ev.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
      ev.button.windowID = wid;
      ev.button.button = static_cast<uint8_t>(m.button);
      ev.button.down = down;
      ev.button.clicks = 1;
      ev.button.x = m.x;
      ev.button.y = m.y;
      break;
    }
    case netinput::KIND_MOUSE_WHEEL: {
      ev.type = SDL_EVENT_MOUSE_WHEEL;
      ev.wheel.windowID = wid;
      ev.wheel.x = m.wheel_x;
      ev.wheel.y = m.wheel_y;
      break;
    }
    default:
      return;
    }

    SDL_PushEvent(&ev);
  }
};
