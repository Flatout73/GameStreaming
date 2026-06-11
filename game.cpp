
#include "VEInclude.h"
#include "VHInclude.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <functional>
#include <iostream>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "event_receiver.cpp"
#include "frame_encoder.cpp"

// Snake 3D — classic grid snake on the XY plane with 3D graphics and a
// Snake III (2005) style chase camera behind the snake's head.
// Fruits grow the snake and score points; stones and the border wall as
// well as the snake itself are lethal; the countdown timer ends the game.
class MyGame : public vve::System {

  enum class State : int { RUNNING, DEAD };
  enum class DeathReason : int { NONE, SELF, STONE, WALL, TIME };

  // ----- tuning -----
  // field covers the measured SOLID interior of the cobblestone terrain;
  // the mesh boundary is ragged and has holes near x>=11, y<=-21
  static constexpr int c_grid_x = 25;           // cells west-east
  static constexpr int c_grid_y = 41;           // cells south-north
  static constexpr float c_cell = 1.0f;         // world units per cell
  static constexpr int c_init_len = 3;
  static constexpr int c_num_stones = 26;       // same density as 12 on 21x21
  static constexpr int c_num_fruits = 7;        // fruits on the field at once
  static constexpr float c_step_time0 = 0.16f;  // seconds per grid step
  static constexpr float c_step_time_min = 0.10f;
  static constexpr float c_max_time = 90.0f;
  static constexpr float c_bonus_time = 2.0f;   // seconds gained per fruit
  static constexpr float c_cam_dist = 5.5f;
  static constexpr float c_cam_height = 3.5f;
  static constexpr float c_cam_look_ahead = 3.0f;
  static constexpr float c_cam_lerp = 8.0f;     // camera yaw smoothing (1/s)
  static constexpr float c_head_lerp = 14.0f;   // head yaw smoothing (1/s)
  static constexpr float c_half_pi = 1.5707963f;
  inline static const vec3_t c_park{0.0f, 0.0f, -50.0f}; // pooled-object parking

  struct FruitType {
    const char *file;
    const char *label;
    int points;
    int growth;
    float scale;
  };
  inline static const std::array<FruitType, 4> c_fruit_types{{
      {"assets/fruits/apple.obj", "Apple", 10, 1, 3.0f},
      {"assets/fruits/orange.obj", "Orange", 20, 2, 3.2f},
      {"assets/fruits/banana.obj", "Banana", 30, 3, 1.5f},
      {"assets/fruits/cherries.obj", "Cherries", 50, 5, 3.2f},
  }};

  struct Fruit {
    vecs::Handle handle{};
    int type{0};
    glm::ivec2 cell{-1, -1};
    bool active{false};
    float spin{0.0f};
    float bobPhase{0.0f};
  };

public:
  MyGame(vve::Engine &engine) : vve::System("MyGame", engine) {

    m_engine.RegisterCallbacks(
        {{this, 0, "LOAD_LEVEL",
          [this](Message &message) { return OnLoadLevel(message); }},
         {this, 10000, "UPDATE",
          [this](Message &message) { return OnUpdate(message); }},
         {this, -10000, "RECORD_NEXT_FRAME",
          [this](Message &message) { return OnRecordNextFrame(message); }},
         {this, -1000, "SDL_KEY_DOWN",
          [this](Message &message) { return OnKeyDown(message); }},
         {this, -1000, "SDL_KEY_REPEAT",
          [this](Message &message) { return OnKeyRepeat(message); }}});
    m_engine.SetVolume(m_volume);
  };

  ~MyGame() {};

  void GetCamera() {
    if (m_cameraHandle.IsValid() == false) {
      auto [handle, camera, parent] =
          *m_registry.GetView<vecs::Handle, vve::Camera &, vve::ParentHandle>()
               .begin();
      m_cameraHandle = handle;
      m_cameraNodeHandle = parent;
    };
  }

  // ----- grid helpers -----

  vec3_t CellCenter(glm::ivec2 c, float z) const {
    const float offX = (c_grid_x - 1) / 2.0f;
    const float offY = (c_grid_y - 1) / 2.0f;
    return vec3_t{(c.x - offX) * c_cell, (c.y - offY) * c_cell, z};
  }

  static glm::ivec2 Turn(glm::ivec2 h, int dir) { // +1 left (CCW), -1 right
    return dir > 0 ? glm::ivec2{-h.y, h.x} : glm::ivec2{h.y, -h.x};
  }

  bool InField(glm::ivec2 c) const {
    return c.x >= 0 && c.x < c_grid_x && c.y >= 0 && c.y < c_grid_y;
  }

  bool IsStone(glm::ivec2 c) const {
    return std::find(m_stoneCells.begin(), m_stoneCells.end(), c) !=
           m_stoneCells.end();
  }

  bool IsFree(glm::ivec2 c) const {
    if (!InField(c) || IsStone(c)) return false;
    if (std::find(m_snake.begin(), m_snake.end(), c) != m_snake.end())
      return false;
    for (auto &f : m_fruits)
      if (f.active && f.cell == c) return false;
    return true;
  }

  glm::ivec2 RandomFreeCell() {
    std::uniform_int_distribution<int> distX(0, c_grid_x - 1);
    std::uniform_int_distribution<int> distY(0, c_grid_y - 1);
    for (int i = 0; i < 200; i++) {
      glm::ivec2 c{distX(m_rng), distY(m_rng)};
      if (IsFree(c)) return c;
    }
    for (int x = 0; x < c_grid_x; x++) // deterministic fallback
      for (int y = 0; y < c_grid_y; y++)
        if (IsFree({x, y})) return {x, y};
    return {-1, -1}; // field completely full
  }

  // ----- level construction -----

  // Rock1.obj ships a hidden untextured 12x12 "Plane" quad next to the rock
  // mesh; remove that child from every instantiated rock scene.
  void DestroyChildrenNamed(vecs::Handle root, const std::string &needle) {
    std::vector<vecs::Handle> doomed;
    std::function<void(vecs::Handle)> dfs = [&](vecs::Handle h) {
      if (!m_registry.Has<vve::Children>(h)) return;
      for (auto child : m_registry.Get<vve::Children &>(h)()) {
        std::string name;
        if (m_registry.Has<vve::Name>(child))
          name = m_registry.Get<vve::Name &>(child)();
        if (name.find(needle) != std::string::npos)
          doomed.push_back(child);
        else
          dfs(child);
      }
    };
    dfs(root);
    for (auto h : doomed) m_engine.DestroyObject(vve::ObjectHandle{h});
  }

  bool OnLoadLevel(Message message) {
    auto msg = message.template GetData<vve::System::MsgLoadLevel>();
    std::cout << "Loading level: " << msg.m_level << std::endl;

    // ground: cobblestone is 14.3 x 25.2 units, off-center; at scale 2 and
    // this position it is centered under the field with cobble tops at z~0
    m_engine.CreateScene(
        vve::Name{}, vve::ParentHandle{},
        vve::Filename{"assets/test/cobblestone/Stone_ground_01.obj"},
        aiProcess_Triangulate, vve::Position{vec3_t{0.31f, -5.85f, -1.24f}},
        vve::Rotation{mat3_t{1.0f}}, vve::Scale{vec3_t{2.0f, 2.0f, 2.0f}});

    // grass backstop below the terrain: the cobblestone mesh has ragged
    // edges and a few real holes, this fills them with ground, not sky
    m_engine.LoadScene(vve::Filename{"assets/test/plane/plane_t_n_s.obj"},
                       aiProcess_FlipWindingOrder);
    m_engine.CreateObject(
        vve::Name{}, vve::ParentHandle{},
        vve::MeshName{"assets/test/plane/plane_t_n_s.obj/plane"},
        vve::TextureName{"assets/test/plane/grass.jpg"},
        vve::Position{vec3_t{0.0f, 0.0f, -1.30f}},
        vve::Rotation{mat3_t{glm::rotate(mat4_t{1.0f}, c_half_pi,
                                         vec3_t{1.0f, 0.0f, 0.0f})}},
        vve::Scale{vec3_t{60.0f}}, vve::UVScale{vec2_t{30.0f}});

    // border wall: 4 stretched gray cubes at the terrain edges
    m_engine.LoadScene(vve::Filename{"assets/standard/cube.obj"});
    const vvh::Color wallColor{{0.06f, 0.06f, 0.07f, 1.0f},
                               {0.45f, 0.45f, 0.48f, 1.0f},
                               {0.10f, 0.10f, 0.10f, 1.0f}};
    const float edgeX = c_grid_x * c_cell / 2.0f + 0.5f; // 13.0
    const float edgeY = c_grid_y * c_cell / 2.0f + 0.5f; // 21.0
    const float lenX = c_grid_x * c_cell + 2.0f;         // covers corners
    const float lenY = c_grid_y * c_cell + 2.0f;
    for (int i = 0; i < 4; i++) {
      bool ns = i < 2; // north/south walls run west-east
      float sign = (i % 2 == 0) ? 1.0f : -1.0f;
      m_engine.CreateObject(
          vve::Name{}, vve::ParentHandle{},
          vve::MeshName{"assets/standard/cube.obj/cube"}, wallColor,
          vve::Position{ns ? vec3_t{0.0f, sign * edgeY, 0.5f}
                            : vec3_t{sign * edgeX, 0.0f, 0.5f}},
          vve::Rotation{mat3_t{1.0f}},
          vve::Scale{ns ? vec3_t{lenX, 1.0f, 1.0f} : vec3_t{1.0f, lenY, 1.0f}},
          vve::UVScale{vec2_t{1.0f}});
    }

    // stones (positions assigned by RandomizeStones)
    const mat3_t upright{
        glm::rotate(mat4_t{1.0f}, c_half_pi, vec3_t{1.0f, 0.0f, 0.0f})};
    for (int i = 0; i < c_num_stones; i++) {
      auto handle = m_engine.CreateScene(
          vve::Name{}, vve::ParentHandle{},
          vve::Filename{"assets/test/box/Rock1.obj"}, aiProcess_Triangulate,
          vve::Position{c_park}, vve::Rotation{upright},
          vve::Scale{vec3_t{0.3f}});
      DestroyChildrenNamed(handle, "Plane");
      m_stoneHandles.push_back(handle);
    }

    // snake head + initial body pool (generated models, Z-up, facing +Y)
    m_headHandle = m_engine.CreateScene(
        vve::Name{}, vve::ParentHandle{},
        vve::Filename{"assets/snake/snake_head.obj"}, aiProcess_Triangulate,
        vve::Position{c_park}, vve::Rotation{mat3_t{1.0f}},
        vve::Scale{vec3_t{0.95f}});
    EnsureSegments(c_init_len - 1);

    // fruit pools: c_num_fruits instances per type, parked off-field
    for (int t = 0; t < (int)c_fruit_types.size(); t++) {
      for (int k = 0; k < c_num_fruits; k++) {
        Fruit f;
        f.type = t;
        f.handle = m_engine.CreateScene(
            vve::Name{}, vve::ParentHandle{},
            vve::Filename{c_fruit_types[t].file}, aiProcess_Triangulate,
            vve::Position{c_park}, vve::Rotation{upright},
            vve::Scale{vec3_t{c_fruit_types[t].scale}});
        m_fruits.push_back(f);
      }
    }

    // chase camera drives the camera NODE; the camera entity itself stays at
    // identity (the engine only recomputes the view matrix when the node moves)
    GetCamera();
    m_registry.Get<vve::Position &>(m_cameraHandle)() = vec3_t{0.0f};
    m_registry.Get<vve::Rotation &>(m_cameraHandle)() = mat3_t{1.0f};

    ResetGame();

    m_engine.PlaySound(vve::Filename{"assets/sounds/dance.mp3"}, -1, 50);
    m_engine.SetVolume(m_volume);
    return false;
  };

  // ----- game state -----

  void EnsureSegments(size_t n) {
    while (m_segPool.size() < n) {
      m_segPool.push_back(m_engine.CreateScene(
          vve::Name{}, vve::ParentHandle{},
          vve::Filename{"assets/snake/snake_body.obj"}, aiProcess_Triangulate,
          vve::Position{c_park}, vve::Rotation{mat3_t{1.0f}},
          vve::Scale{vec3_t{1.0f}}));
    }
  }

  void RandomizeStones() {
    m_stoneCells.clear();
    std::uniform_int_distribution<int> distX(1, c_grid_x - 2);
    std::uniform_int_distribution<int> distY(1, c_grid_y - 2);
    while ((int)m_stoneCells.size() < c_num_stones) {
      glm::ivec2 c{distX(m_rng), distY(m_rng)};
      bool inSpawnCorridor = std::abs(c.x - c_grid_x / 2) <= 1 && c.y >= 2;
      if (inSpawnCorridor || IsStone(c)) continue;
      m_stoneCells.push_back(c);
    }
    for (int i = 0; i < c_num_stones; i++) {
      m_registry.Get<vve::Position &>(m_stoneHandles[i])() =
          CellCenter(m_stoneCells[i], 0.10f);
    }
  }

  void ParkFruit(Fruit &f) {
    f.active = false;
    f.cell = {-1, -1};
    m_registry.Get<vve::Position &>(f.handle)() = c_park;
  }

  void SpawnFruit() {
    std::vector<int> types; // types that still have a parked instance
    for (int t = 0; t < (int)c_fruit_types.size(); t++)
      for (auto &f : m_fruits)
        if (f.type == t && !f.active) {
          types.push_back(t);
          break;
        }
    if (types.empty()) return;
    int type = types[std::uniform_int_distribution<int>(
        0, (int)types.size() - 1)(m_rng)];
    glm::ivec2 cell = RandomFreeCell();
    if (cell.x < 0) return; // field full, skip
    for (auto &f : m_fruits) {
      if (f.type != type || f.active) continue;
      f.active = true;
      f.cell = cell;
      f.spin = 0.0f;
      f.bobPhase =
          std::uniform_real_distribution<float>(0.0f, 6.28f)(m_rng);
      m_registry.Get<vve::Position &>(f.handle)() = CellCenter(cell, 0.05f);
      return;
    }
  }

  void ResetGame() {
    m_state = State::RUNNING;
    m_deathReason = DeathReason::NONE;
    m_score = 0;
    m_timeLeft = c_max_time;
    m_stepTime = c_step_time0;
    m_pendingGrowth = 0;
    m_accum = 0.0f;
    m_turnQueue.clear();

    m_snake.clear();
    const int mid = c_grid_x / 2;
    for (int i = 0; i < c_init_len; i++)
      m_snake.push_back({mid, 4 - i}); // head (10,4), body below, heading +Y
    m_heading = {0, 1};
    m_prevSnake = m_snake;
    m_camYaw = m_headYaw = c_half_pi;

    RandomizeStones();
    for (auto &f : m_fruits) ParkFruit(f);
    for (int i = 0; i < c_num_fruits; i++) SpawnFruit();

    for (auto h : m_segPool)
      m_registry.Get<vve::Position &>(h)() = c_park;
    UpdateSnakeVisuals(0.0f);
    UpdateCamera(0.0f, 0.0f);
  }

  void Restart() {
    std::cout << "[game] restart" << std::endl;
    ResetGame();
    m_engine.PlaySound(vve::Filename{"assets/sounds/dance.mp3"}, -1, 50);
  }

  void Die(DeathReason reason) {
    std::cout << "[game] game over: " << DeathTextFor(reason)
              << " score=" << m_score << " len=" << m_snake.size()
              << std::endl;
    m_state = State::DEAD;
    m_deathReason = reason;
    m_prevSnake = m_snake;
    m_accum = 0.0f;
    m_engine.PlaySound(vve::Filename{"assets/sounds/dance.mp3"}, 0, 50);
    if (reason != DeathReason::TIME)
      m_engine.PlaySound(vve::Filename{"assets/sounds/explosion.wav"}, 1, 80);
    m_engine.PlaySound(vve::Filename{"assets/sounds/gameover.wav"}, 1, 80);
  }

  void StepSnake() {
    if (!m_turnQueue.empty()) {
      m_heading = Turn(m_heading, m_turnQueue.front());
      m_turnQueue.pop_front();
    }
    glm::ivec2 next = m_snake.front() + m_heading;

    if (!InField(next)) return Die(DeathReason::WALL);
    if (IsStone(next)) return Die(DeathReason::STONE);
    // self collision: the tail cell is vacated this step unless we grow
    for (size_t i = 0; i < m_snake.size(); i++) {
      if (m_pendingGrowth == 0 && i == m_snake.size() - 1) break;
      if (m_snake[i] == next) return Die(DeathReason::SELF);
    }

    m_prevSnake = m_snake;
    m_snake.push_front(next);

    for (auto &f : m_fruits) {
      if (!f.active || f.cell != next) continue;
      m_score += c_fruit_types[f.type].points;
      m_pendingGrowth += c_fruit_types[f.type].growth;
      m_timeLeft = std::min(m_timeLeft + c_bonus_time, 999.0f);
      m_stepTime = std::max(c_step_time_min,
                            c_step_time0 - 0.002f * (m_score / 10.0f));
      m_engine.PlaySound(vve::Filename{"assets/sounds/bell.wav"}, 1, 80);
      ParkFruit(f);
      SpawnFruit();
      break;
    }

    if (m_pendingGrowth > 0) {
      m_pendingGrowth--;
      EnsureSegments(m_snake.size() - 1);
    } else {
      m_snake.pop_back();
    }
  }

  // ----- per-frame visuals -----

  vec3_t LerpedCell(size_t i, float t, float z) const {
    glm::ivec2 cur = m_snake[i];
    glm::ivec2 prev = (i < m_prevSnake.size()) ? m_prevSnake[i] : cur;
    return glm::mix(CellCenter(prev, z), CellCenter(cur, z), t);
  }

  void UpdateSnakeVisuals(float t) {
    m_registry.Get<vve::Position &>(m_headHandle)() = LerpedCell(0, t, 0.0f);
    m_registry.Get<vve::Rotation &>(m_headHandle)() = mat3_t{glm::rotate(
        mat4_t{1.0f}, m_headYaw - c_half_pi, vec3_t{0.0f, 0.0f, 1.0f})};
    for (size_t j = 0; j < m_segPool.size(); j++) {
      m_registry.Get<vve::Position &>(m_segPool[j])() =
          (j + 1 < m_snake.size()) ? LerpedCell(j + 1, t, 0.0f) : c_park;
    }
  }

  void UpdateFruitVisuals(float dt) {
    for (auto &f : m_fruits) {
      if (!f.active) continue;
      f.spin += dt * 1.5f;
      float bob = 0.05f + 0.08f * (0.5f + 0.5f * sinf(2.2f * f.spin + f.bobPhase));
      m_registry.Get<vve::Position &>(f.handle)() = CellCenter(f.cell, bob);
      m_registry.Get<vve::Rotation &>(f.handle)() = mat3_t{
          glm::rotate(mat4_t{1.0f}, f.spin, vec3_t{0.0f, 0.0f, 1.0f}) *
          glm::rotate(mat4_t{1.0f}, c_half_pi, vec3_t{1.0f, 0.0f, 0.0f})};
    }
  }

  static float ApproachAngle(float current, float target, float rate, float dt) {
    float d = remainderf(target - current, 6.2831853f);
    return current + d * std::min(1.0f, rate * dt);
  }

  void UpdateCamera(float dt, float t) {
    if (!m_cameraNodeHandle.IsValid()) return;
    vec3_t headW = LerpedCell(0, t, 0.35f);
    float target = atan2f((float)m_heading.y, (float)m_heading.x);
    m_camYaw = ApproachAngle(m_camYaw, target, c_cam_lerp, dt);
    vec3_t fwd{cosf(m_camYaw), sinf(m_camYaw), 0.0f};
    vec3_t eye = headW - fwd * c_cam_dist + vec3_t{0.0f, 0.0f, c_cam_height};
    vec3_t center = headW + fwd * c_cam_look_ahead;
    mat4_t camToWorld =
        glm::inverse(glm::lookAt(eye, center, vec3_t{0.0f, 0.0f, 1.0f}));
    m_registry.Get<vve::Position &>(m_cameraNodeHandle)() =
        vec3_t{camToWorld[3]};
    m_registry.Get<vve::Rotation &>(m_cameraNodeHandle)() = mat3_t{camToWorld};
    // keep the camera entity pinned; the node carries the full pose
    m_registry.Get<vve::Position &>(m_cameraHandle)() = vec3_t{0.0f};
    m_registry.Get<vve::Rotation &>(m_cameraHandle)() = mat3_t{1.0f};
  }

  // ----- callbacks -----

  bool OnUpdate(Message &message) {
    auto msg = message.template GetData<vve::System::MsgUpdate>();
    float dt = std::min((float)msg.m_dt, 0.1f);
    GetCamera();

    if (m_state == State::RUNNING) {
      m_timeLeft -= dt;
      if (m_timeLeft <= 0.0f) {
        m_timeLeft = 0.0f;
        Die(DeathReason::TIME);
      }
    }
    if (m_state == State::RUNNING) {
      m_accum += dt;
      while (m_accum >= m_stepTime && m_state == State::RUNNING) {
        m_accum -= m_stepTime;
        StepSnake();
      }
    }

    float headTarget = atan2f((float)m_heading.y, (float)m_heading.x);
    m_headYaw = ApproachAngle(m_headYaw, headTarget, c_head_lerp, dt);

    float t = (m_state == State::RUNNING)
                  ? std::clamp(m_accum / m_stepTime, 0.0f, 1.0f)
                  : 0.0f;
    UpdateSnakeVisuals(t);
    UpdateFruitVisuals(dt);
    UpdateCamera(dt, t);
    return false;
  }

  bool OnKeyDown(Message message) {
    auto msg = message.template GetData<vve::System::MsgKeyDown>();
    switch (msg.m_key) {
    case SDL_SCANCODE_LEFT:
    case SDL_SCANCODE_A:
      if (m_state == State::RUNNING && m_turnQueue.size() < 2)
        m_turnQueue.push_back(+1);
      return true;
    case SDL_SCANCODE_RIGHT:
    case SDL_SCANCODE_D:
      if (m_state == State::RUNNING && m_turnQueue.size() < 2)
        m_turnQueue.push_back(-1);
      return true;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:
    case SDL_SCANCODE_SPACE:
      if (m_state == State::DEAD) Restart();
      return true;
    // swallow the engine GUI's free-fly camera keys
    case SDL_SCANCODE_W:
    case SDL_SCANCODE_S:
    case SDL_SCANCODE_Q:
    case SDL_SCANCODE_E:
    case SDL_SCANCODE_UP:
    case SDL_SCANCODE_DOWN:
      return true;
    }
    return false; // ESC, O, P etc. stay with the engine
  }

  bool OnKeyRepeat(Message message) {
    auto msg = message.template GetData<vve::System::MsgKeyRepeat>();
    switch (msg.m_key) {
    case SDL_SCANCODE_LEFT:
    case SDL_SCANCODE_A:
    case SDL_SCANCODE_RIGHT:
    case SDL_SCANCODE_D:
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:
    case SDL_SCANCODE_SPACE:
    case SDL_SCANCODE_W:
    case SDL_SCANCODE_S:
    case SDL_SCANCODE_Q:
    case SDL_SCANCODE_E:
    case SDL_SCANCODE_UP:
    case SDL_SCANCODE_DOWN:
      return true; // held keys must not reach the GUI camera either
    }
    return false;
  }

  static const char *DeathTextFor(DeathReason reason) {
    switch (reason) {
    case DeathReason::SELF: return "You bit yourself!";
    case DeathReason::STONE: return "You hit a stone!";
    case DeathReason::WALL: return "You hit the wall!";
    case DeathReason::TIME: return "Time is up!";
    default: return "";
    }
  }

  const char *DeathText() const { return DeathTextFor(m_deathReason); }

  bool OnRecordNextFrame(Message message) {
    ImGuiIO &io = ImGui::GetIO();

    // ---- Settings button in the upper-right corner ----
    const float margin = 10.0f;
    const float btnW = 80.0f, btnH = 28.0f;
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - btnW - margin, margin),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(btnW + 4.0f, btnH + 4.0f),
                             ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::Begin("##settings_btn", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    if (ImGui::Button("Settings", ImVec2(btnW, btnH)))
      m_show_settings = !m_show_settings;
    ImGui::End();

    if (m_show_settings) {
      ImGui::SetNextWindowPos(
          ImVec2(io.DisplaySize.x - 160.0f - margin, margin + btnH + 6.0f),
          ImGuiCond_Always);
      ImGui::SetNextWindowSize(ImVec2(160.0f, 60.0f), ImGuiCond_Always);
      ImGui::Begin("Settings", &m_show_settings,
                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                       ImGuiWindowFlags_NoSavedSettings);
      if (ImGui::Button("Exit", ImVec2(-1.0f, 0.0f)))
        exit(0);
      ImGui::End();
    }

    // ---- HUD ----
    ImGui::SetNextWindowPos(ImVec2(margin, margin), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGui::Begin("##hud", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoNav);
    ImGui::SetWindowFontScale(1.5f);
    ImGui::Text("Score: %d", m_score);
    ImGui::Text("Time:  %.0f s", m_timeLeft);
    ImGui::Text("Length: %zu", m_snake.size());
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Separator();
    for (auto &ft : c_fruit_types)
      ImGui::Text("%s: %d", ft.label, ft.points);
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::SliderFloat("Volume", &m_volume, 0, MIX_MAX_VOLUME, "%.0f")) {
      m_engine.SetVolume(m_volume);
    }
    ImGui::End();

    // ---- game over ----
    if (m_state == State::DEAD) {
      ImGui::SetNextWindowPos(
          ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.45f),
          ImGuiCond_Always, ImVec2(0.5f, 0.5f));
      ImGui::Begin("Game Over", nullptr,
                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                       ImGuiWindowFlags_NoCollapse |
                       ImGuiWindowFlags_AlwaysAutoResize |
                       ImGuiWindowFlags_NoSavedSettings);
      ImGui::SetWindowFontScale(1.8f);
      ImGui::TextUnformatted("GAME OVER");
      ImGui::SetWindowFontScale(1.2f);
      ImGui::TextUnformatted(DeathText());
      ImGui::Text("Final Score: %d", m_score);
      ImGui::Text("Snake Length: %zu", m_snake.size());
      ImGui::SetWindowFontScale(1.0f);
      ImGui::Spacing();
      if (ImGui::Button("Restart (Enter)", ImVec2(-1.0f, 0.0f)))
        Restart();
      ImGui::End();
    }

    DrawSceneInspector();
    return false;
  }

  // Scene inspector: lists every object, shows its name/type/position,
  // and exposes a color picker for lights
  void DrawSceneInspector() {
    ImGui::SetNextWindowSize(ImVec2(360.0f, 420.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowCollapsed(true, ImGuiCond_FirstUseEver); // dev tool
    ImGui::Begin("Scene");

    int count = 0;
    for (auto [handle, pos] :
         m_registry.GetView<vecs::Handle, vve::Position &>()) {
      ++count;
    }
    ImGui::Text("Objects in scene: %d", count);
    ImGui::Separator();

    int idx = 0;
    for (auto [handle, pos] :
         m_registry.GetView<vecs::Handle, vve::Position &>()) {
      ImGui::PushID(idx++);

      std::string name = "(unnamed)";
      if (m_registry.Has<vve::Name>(handle)) {
        std::string n = m_registry.Get<vve::Name &>(handle)();
        if (!n.empty())
          name = n;
      }

      const char *type = "Object";
      int lightKind = 0; // 0=none, 1=point, 2=directional, 3=spot
      if (m_registry.Has<vve::Camera>(handle)) {
        type = "Camera";
      } else if (m_registry.Has<vve::PointLight>(handle)) {
        type = "PointLight";
        lightKind = 1;
      } else if (m_registry.Has<vve::DirectionalLight>(handle)) {
        type = "DirectionalLight";
        lightKind = 2;
      } else if (m_registry.Has<vve::SpotLight>(handle)) {
        type = "SpotLight";
        lightKind = 3;
      } else if (m_registry.Has<vve::Children>(handle)) {
        type = "Node";
      }

      vec3_t p = pos();
      if (ImGui::TreeNode("node", "%s  [%s]", name.c_str(), type)) {
        ImGui::Text("Position: %.2f, %.2f, %.2f", p.x, p.y, p.z);

        if (lightKind != 0) {
          vvh::LightParams *params = nullptr;
          if (lightKind == 1)
            params = &m_registry.Get<vve::PointLight &>(handle)();
          else if (lightKind == 2)
            params = &m_registry.Get<vve::DirectionalLight &>(handle)();
          else
            params = &m_registry.Get<vve::SpotLight &>(handle)();

          glm::vec3 c = params->color;
          float col[3] = {c.r, c.g, c.b};
          if (ImGui::ColorEdit3("Light color", col)) {
            params->color = glm::vec3{col[0], col[1], col[2]};
          }
        }
        ImGui::TreePop();
      }
      ImGui::PopID();
    }

    ImGui::End();
  }

private:
  // game state
  State m_state = State::RUNNING;
  DeathReason m_deathReason = DeathReason::NONE;
  std::deque<glm::ivec2> m_snake, m_prevSnake; // head at front
  glm::ivec2 m_heading{0, 1};
  std::deque<int> m_turnQueue; // +1 left, -1 right; max 2 buffered
  int m_pendingGrowth = 0;
  int m_score = 0;
  float m_timeLeft = c_max_time;
  float m_stepTime = c_step_time0;
  float m_accum = 0.0f;
  std::mt19937 m_rng{std::random_device{}()};

  // world objects
  std::vector<glm::ivec2> m_stoneCells;
  std::vector<vecs::Handle> m_stoneHandles;
  std::vector<vecs::Handle> m_segPool; // body segment objects (pooled)
  vecs::Handle m_headHandle{};
  std::vector<Fruit> m_fruits;

  // camera
  vecs::Handle m_cameraHandle{};
  vecs::Handle m_cameraNodeHandle{};
  float m_camYaw = c_half_pi;
  float m_headYaw = c_half_pi;

  // UI
  float m_volume{MIX_MAX_VOLUME / 2.0};
  bool m_show_settings{false};
};

int main() {
  vve::Engine engine("My Engine", vve::RendererType::RENDERER_TYPE_FORWARD);
  MyGame mygui{engine};
  FrameEncoder encoder{engine};
  EventReceiver eventReceiver{engine};
  engine.Run();

  return 0;
}
