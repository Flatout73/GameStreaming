
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include "VEInclude.h"
#include "VHInclude.h"
#include <cstdlib>
#include <format>
#include <fstream>
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
        aiProcess_Triangulate, vve::Position{vec3_t{0.0f, 0.0f, -0.7f}},
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
    const float btnW = 80.0f, btnH = 28.0f;
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
    if (ImGui::Button("Settings", ImVec2(btnW, btnH)))
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

// ---------------------------------------------------------------------------
// FrameEncoder – captures every rendered frame and encodes it to H.264
// Follows the pattern from VEGUI.cpp (frame capture) and Store (FFmpeg encode)
// ---------------------------------------------------------------------------
class FrameEncoder : public vve::System {
public:
  FrameEncoder(vve::Engine &engine, std::string windowName = "")
      : vve::System("FrameEncoder", engine),
        m_windowName(std::move(windowName)) {

    // --- Find the H.264 encoder ---
    m_codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!m_codec) {
      std::cerr << "FrameEncoder: H.264 codec not found" << std::endl;
      return;
    }

    // --- Register for FRAME_END (like VEGUI.cpp) ---
    m_engine.RegisterCallbacks(
        {{this, 0, "FRAME_END",
          [this](Message &message) { return OnFrameEnd(message); }}});
  }

  ~FrameEncoder() { Shutdown(); }

private:
  // ---- Encoder state ----
  const AVCodec *m_codec{nullptr};
  AVCodecContext *m_codecCtx{nullptr};
  AVPacket *m_pkt{nullptr};
  AVFrame *m_yuvFrame{nullptr};
  SwsContext *m_swsCtx{nullptr};
  std::ofstream m_outFile;
  bool m_initialized{false};
  int64_t m_frameIndex{0};
  std::string m_windowName;

  static constexpr int c_fps = 30;
  static constexpr int64_t c_bitrate = 2000000;
  static constexpr int64_t c_reducedBitrate = 500000;
  static constexpr int c_bitrateSwitchFrame = 5 * 30; // Switch after 5 seconds
  static constexpr const char *c_outputH264 = "output.h264";
  static constexpr const char *c_outputMP4 = "output.mp4";

  bool m_bitrateReduced{false};

  // ---- Lazy-init the encoder once we know the resolution ----
  bool InitEncoder(int width, int height) {
    m_codecCtx = avcodec_alloc_context3(m_codec);
    if (!m_codecCtx) {
      std::cerr << "FrameEncoder: could not allocate codec context"
                << std::endl;
      return false;
    }

    m_codecCtx->bit_rate = c_bitrate;
    m_codecCtx->width = width;
    m_codecCtx->height = height;
    m_codecCtx->time_base = {1, c_fps};
    m_codecCtx->framerate = {c_fps, 1};
    m_codecCtx->gop_size = 10;
    m_codecCtx->max_b_frames = 1;
    m_codecCtx->pix_fmt = AV_PIX_FMT_YUV420P;

    if (avcodec_open2(m_codecCtx, m_codec, nullptr) < 0) {
      std::cerr << "FrameEncoder: could not open codec" << std::endl;
      avcodec_free_context(&m_codecCtx);
      return false;
    }

    m_outFile.open(c_outputH264, std::ios::binary);
    if (!m_outFile) {
      std::cerr << "FrameEncoder: could not open output file" << std::endl;
      avcodec_free_context(&m_codecCtx);
      return false;
    }

    m_pkt = av_packet_alloc();

    // Pre-allocate the reusable YUV frame
    m_yuvFrame = av_frame_alloc();
    m_yuvFrame->format = AV_PIX_FMT_YUV420P;
    m_yuvFrame->width = width;
    m_yuvFrame->height = height;
    av_image_alloc(m_yuvFrame->data, m_yuvFrame->linesize, width, height,
                   AV_PIX_FMT_YUV420P, 32);

    // SWS context: RGBA (from Vulkan) -> YUV420P
    m_swsCtx = sws_getContext(width, height, AV_PIX_FMT_RGBA, width, height,
                              AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr,
                              nullptr, nullptr);
    if (!m_swsCtx) {
      std::cerr << "FrameEncoder: could not create SwsContext" << std::endl;
      return false;
    }

    m_initialized = true;
    std::cout << "FrameEncoder: initialized " << width << "x" << height
              << std::endl;
    return true;
  }

  // ---- Called every frame after rendering (like VEGUI::OnFrameEnd) ----
  bool OnFrameEnd(Message message) {
    if (!m_codec)
      return false;

    // 1. Get Vulkan + Window state (same as VEGUI.cpp)
    auto vstate = std::get<1>(vve::Renderer::GetState(m_registry));
    auto wstate = std::get<1>(vve::Window::GetState(m_registry, m_windowName));

    // Use swapChainExtent for physical pixel resolution instead of logical window size (fixes High DPI / Retina scaling)
    VkExtent2D extent = vstate().m_swapChain.m_swapChainExtent;
    uint32_t imageSize = extent.width * extent.height * 4; // RGBA
    VkImage image = vstate().m_swapChain.m_swapChainImages[vstate().m_imageIndex];

    // Lazy-init on first frame (now we know the resolution)
    if (!m_initialized) {
      if (!InitEncoder(extent.width, extent.height))
        return false;
    }

    // 2. Copy the swapchain image to host memory (same as VEGUI.cpp)
    uint8_t *dataImage = new uint8_t[imageSize];
    vvh::ImgCopyImageToHost({
        vstate().m_device, vstate().m_vmaAllocator, vstate().m_graphicsQueue,
        vstate().m_commandPool, image, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, dataImage,
        extent.width, extent.height, imageSize, 2, 1, 0,
        3 // channel swap indices r,g,b,a
    });

    // 3. Wrap the raw Vulkan image in an RGB AVFrame (Store pattern)
    AVFrame *rgbFrame = av_frame_alloc();
    rgbFrame->format = AV_PIX_FMT_RGBA;
    rgbFrame->width = extent.width;
    rgbFrame->height = extent.height;
    
    // Wire up the rgbFrame's data array to point to our captured pixels
    av_image_fill_arrays(rgbFrame->data, rgbFrame->linesize, dataImage,
                         AV_PIX_FMT_RGBA, extent.width, extent.height, 1);

    // Convert RGBA AVFrame -> YUV420P AVFrame (Store pattern)
    sws_scale(m_swsCtx, rgbFrame->data, rgbFrame->linesize, 0, extent.height, 
              m_yuvFrame->data, m_yuvFrame->linesize);

    m_yuvFrame->pts = m_frameIndex; // Store pattern uses raw index for YUV pts

    // 4. Encode (Store pattern with Bitrate Switching)
    if (avcodec_send_frame(m_codecCtx, m_yuvFrame) < 0) {
      std::cerr << "Error sending frame to codec" << std::endl;
      av_frame_free(&rgbFrame); // Free the wrapper
      delete[] dataImage;
      return false;
    }

    if (!m_bitrateReduced && m_frameIndex >= c_bitrateSwitchFrame) {
      // Flush original codec context (Store pattern)
      avcodec_send_frame(m_codecCtx, nullptr);
      while (avcodec_receive_packet(m_codecCtx, m_pkt) == 0) {
        m_pkt->pts = m_pkt->dts = m_pkt->pts * m_codecCtx->time_base.den / m_codecCtx->time_base.num;
        m_outFile.write(reinterpret_cast<const char *>(m_pkt->data), m_pkt->size);
        av_packet_unref(m_pkt);
      }
      
      avcodec_free_context(&m_codecCtx);
      m_codecCtx = avcodec_alloc_context3(m_codec);
      if (!m_codecCtx) {
        std::cerr << "FrameEncoder: Could not allocate replacement codec context" << std::endl;
      } else {
        m_codecCtx->bit_rate = c_reducedBitrate;
        m_codecCtx->width = extent.width;
        m_codecCtx->height = extent.height;
        m_codecCtx->time_base = {1, c_fps};
        m_codecCtx->framerate = {c_fps, 1};
        m_codecCtx->gop_size = 10;
        m_codecCtx->max_b_frames = 1;
        m_codecCtx->pix_fmt = AV_PIX_FMT_YUV420P;

        if (avcodec_open2(m_codecCtx, m_codec, nullptr) < 0) {
          std::cerr << "FrameEncoder: Could not reopen codec after bitrate switch" << std::endl;
          avcodec_free_context(&m_codecCtx);
        } else {
          m_bitrateReduced = true;
          std::cout << "FrameEncoder: Switched bitrate to " << c_reducedBitrate << " at frame " << m_frameIndex << std::endl;
        }
      }
    }

    while (avcodec_receive_packet(m_codecCtx, m_pkt) == 0) {
      m_pkt->pts = m_pkt->dts = m_yuvFrame->pts * m_codecCtx->time_base.den / m_codecCtx->time_base.num;
      m_outFile.write(reinterpret_cast<const char *>(m_pkt->data), m_pkt->size);
      av_packet_unref(m_pkt);
    }

    m_frameIndex++;
    av_frame_free(&rgbFrame); // Free the wrapper
    delete[] dataImage;
    return false;
  }

  // ---- Flush encoder, close file, convert to MP4 ----
  void Shutdown() {
    if (!m_initialized)
      return;
    m_initialized = false;

    // Flush the encoder (Store pattern)
    avcodec_send_frame(m_codecCtx, nullptr);
    while (avcodec_receive_packet(m_codecCtx, m_pkt) == 0) {
      m_pkt->pts = m_pkt->dts = m_pkt->pts * m_codecCtx->time_base.den / m_codecCtx->time_base.num;
      m_outFile.write(reinterpret_cast<const char *>(m_pkt->data), m_pkt->size);
      av_packet_unref(m_pkt);
    }

    m_outFile.close();

    // Clean up FFmpeg resources
    av_packet_free(&m_pkt);
    if (m_yuvFrame) {
      av_freep(&m_yuvFrame->data[0]);
      av_frame_free(&m_yuvFrame);
    }
    avcodec_free_context(&m_codecCtx);
    sws_freeContext(m_swsCtx);
    m_swsCtx = nullptr;

    std::cout << "FrameEncoder: encoded " << m_frameIndex << " frames to "
              << c_outputH264 << std::endl;

    // Convert elementary stream to MP4 (Store example: ffmpeg -i output.h264 -c
    // copy output.mp4)
    std::string cmd = std::string("/opt/homebrew/bin/ffmpeg -y -i ") +
                      c_outputH264 + " -c copy " + c_outputMP4;
    std::cout << "FrameEncoder: running: " << cmd << std::endl;
    std::system(cmd.c_str());
    std::cout << "FrameEncoder: created " << c_outputMP4 << std::endl;
  }
};

int main() {
  vve::Engine engine("My Engine", vve::RendererType::RENDERER_TYPE_FORWARD);
  MyGame mygui{engine};
  FrameEncoder encoder{engine};
  engine.Run();

  return 0;
}
