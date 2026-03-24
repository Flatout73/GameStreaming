
#include "VEInclude.h"
#include "VHInclude.h"
#include <format>
#include <iostream>
#include <utility>

class MyGame : public vve::System {

  enum class State : int { STATE_RUNNING, STATE_DEAD };

  const float c_max_time = 35.0f;
  const int c_field_size = 50;
  const int c_number_cubes = 10;

  int nextRandom() { return rand() % (c_field_size)-c_field_size / 2; }

public:
  MyGame(vve::Engine &engine) : vve::System("MyGame", engine) {

    m_engine.RegisterCallbacks(
        {{this, 0, "LOAD_LEVEL",
          [this](Message &message) { return OnLoadLevel(message); }},
         {this, 10000, "UPDATE",
          [this](Message &message) { return OnUpdate(message); }},
         {this, -10000, "RECORD_NEXT_FRAME",
          [this](Message &message) { return OnRecordNextFrame(message); }}});
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

  inline static std::string plane_obj{
      "assets/test/cobblestone/Stone_ground_01.obj"};
  inline static std::string plane_mesh{
      "assets/test/cobblestone/Stone_ground_01.obj/default"};
  inline static std::string plane_txt{
      "assets/test/cobblestone/Stone_ground_01_u1_v1.jpg"};

  inline static std::string cube_obj{"assets/test/box/Rock1.obj"};

  bool OnLoadLevel(Message message) {
    auto msg = message.template GetData<vve::System::MsgLoadLevel>();
    std::cout << "Loading level: " << msg.m_level << std::endl;
    std::string level = std::string("Level: ") + msg.m_level;

    // ----------------- Load Plane -----------------

    m_engine.CreateScene(
        vve::Name{}, vve::ParentHandle{}, vve::Filename{plane_obj},
        aiProcess_Triangulate, vve::Position{vec3_t{0.0f, 0.0f, -0.5f}},
        vve::Rotation{mat4_t{1.0f}}, vve::Scale{vec3_t{2.0f, 2.0f, 2.0f}});

    // ----------------- Load Cube -----------------

    m_handleCube = m_engine.CreateScene(
        vve::Name{}, vve::ParentHandle{}, vve::Filename{cube_obj},
        aiProcess_Triangulate,
        vve::Position{vec3_t{c_field_size / 4.0f, c_field_size / 2.0f, 0.5f}},
        vve::Rotation{mat3_t{
            glm::rotate(mat4_t{1.0f}, 1.57079f, vec3_t{1.0f, 0.0f, 0.0f})}},
        vve::Scale{vec3_t{1.0f}});

    GetCamera();
    m_registry.Get<vve::Rotation &>(m_cameraHandle)() = mat3_t{
        glm::rotate(mat4_t{1.0f}, 3.14152f / 2.0f, vec3_t{1.0f, 0.0f, 0.0f})};

    m_engine.PlaySound(vve::Filename{"assets/sounds/dance.mp3"}, -1, 50);
    m_engine.SetVolume(m_volume);
    return false;
  };

  bool OnUpdate(Message &message) {
    auto msg = message.template GetData<vve::System::MsgUpdate>();
    m_time_left -= static_cast<float>(msg.m_dt);
    auto pos = m_registry.Get<vve::Position &>(m_cameraNodeHandle);
    pos().z = 0.5f;
    if (m_state == State::STATE_RUNNING) {
      if (m_time_left <= 0.0f) {
        m_state = State::STATE_DEAD;

        m_engine.PlaySound(vve::Filename{"assets/sounds/dance.mp3"}, 0, 50);
        m_engine.PlaySound(vve::Filename{"assets/sounds/gameover.wav"}, 1, 50);
        return false;
      }
      auto posCube = m_registry.Get<vve::Position &>(m_handleCube);
      float distance = glm::length(vec2_t{pos().x, pos().y} -
                                   vec2_t{posCube().x, posCube().y});
      // if (distance < 1.5f) {
      //   m_cubes_left--;
      //   posCube().x = static_cast<float>(nextRandom());
      //   posCube().y = static_cast<float>(nextRandom());
      //   if (m_cubes_left == 0) {
      //     m_time_left += 20;
      //     m_cubes_left = c_number_cubes;
      //     m_engine.PlaySound(vve::Filename{"assets/sounds/bell.wav"}, 1);
      //   } else {
      //     m_engine.PlaySound(vve::Filename{"assets/sounds/explosion.wav"},
      //     1);
      //   }
      // }
    }

    return false;
  }

  bool OnRecordNextFrame(Message message) {
    // ---- Settings button in the upper-right corner ----
    ImGuiIO &io = ImGui::GetIO();
    const float margin = 10.0f;
    const float btnW = 36.0f, btnH = 28.0f;
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - btnW - margin, margin),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(btnW + 4.0f, btnH + 4.0f),
                             ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::Begin("##settings_btn", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs * 0 |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    if (ImGui::Button(reinterpret_cast<const char *>(u8"\u2699"),
                      ImVec2(btnW, btnH)))
      m_show_settings = !m_show_settings;
    ImGui::End();

    // ---- Settings popup window ----
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

    if (m_state == State::STATE_RUNNING) {
      ImGui::Begin("Game State");
      char buffer[100];
      std::snprintf(buffer, 100, "Time Left: %.2f s", m_time_left);
      ImGui::TextUnformatted(buffer);
      std::snprintf(buffer, 100, "Cubes Left: %d", m_cubes_left);
      ImGui::TextUnformatted(buffer);
      if (ImGui::SliderFloat("Sound Volume", &m_volume, 0, MIX_MAX_VOLUME)) {
        m_engine.SetVolume(m_volume);
      }
      ImGui::End();
    }

    if (m_state == State::STATE_DEAD) {
      ImGui::Begin("Game State");
      ImGui::TextUnformatted("Game Over");
      if (ImGui::Button("Restart")) {
        m_state = State::STATE_RUNNING;
        m_time_left = c_max_time;
        m_cubes_left = c_number_cubes;
        m_engine.PlaySound(vve::Filename{"assets/sounds/dance.mp3"}, -1);
      }
      ImGui::End();
    }
    return false;
  }

private:
  State m_state = State::STATE_RUNNING;
  float m_time_left = c_max_time;
  int m_cubes_left = c_number_cubes;
  vecs::Handle m_handlePlane{};
  vecs::Handle m_handleCube{};
  vecs::Handle m_cameraHandle{};
  vecs::Handle m_cameraNodeHandle{};
  float m_volume{MIX_MAX_VOLUME / 2.0};
  bool m_show_settings{false};
};

int main() {
  vve::Engine engine("My Engine", vve::RendererType::RENDERER_TYPE_FORWARD);
  MyGame mygui{engine};
  engine.Run();

  return 0;
}
