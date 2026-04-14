extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

// ---------------------------------------------------------------------------
// FrameEncoder – captures every rendered frame and encodes it to H.264
// Follows the pattern from VEGUI.cpp (frame capture) and Store (FFmpeg encode)
// ---------------------------------------------------------------------------
class FrameEncoder : public vve::System {
public:
  FrameEncoder(vve::Engine &engine, std::string windowName = "")
      : vve::System("FrameEncoder", engine),
        m_windowName(std::move(windowName)) {

    // --- Read configuration from Environment Variables ---
    const char *envCodec = std::getenv("ENC_CODEC");
    const char *envBitrate = std::getenv("ENC_BITRATE");
    const char *envOut = std::getenv("ENC_OUT");

    m_codecName = envCodec ? envCodec : "h264";
    m_bitrateVal = envBitrate ? std::stoll(envBitrate) : 2000000;
    m_outFileBase = envOut ? envOut : "output";

    // --- Find the encoder ---
    if (m_codecName == "hevc" || m_codecName == "h265") {
      m_codec = avcodec_find_encoder(AV_CODEC_ID_HEVC);
    } else {
      m_codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    }

    if (!m_codec) {
      std::cerr << "FrameEncoder: " << m_codecName << " codec not found"
                << std::endl;
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
  std::string m_codecName;
  std::string m_outFileBase;
  int64_t m_bitrateVal;

  static constexpr int c_fps = 30;
  static constexpr int c_bitrateSwitchFrame =
      999999; // Disabled for direct comparison

  bool m_bitrateReduced{false};

  bool InitEncoder(int width, int height) {
    m_codecCtx = avcodec_alloc_context3(m_codec);
    if (!m_codecCtx) {
      std::cerr << "FrameEncoder: could not allocate codec context"
                << std::endl;
      return false;
    }

    m_codecCtx->bit_rate = m_bitrateVal;
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

    std::string streamFile = m_outFileBase + "." + m_codecName;
    m_outFile.open(streamFile.c_str(), std::ios::binary);
    if (!m_outFile) {
      std::cerr << "FrameEncoder: could not open output file: " << streamFile
                << std::endl;
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

  bool OnFrameEnd(Message message) {
    if (!m_codec)
      return false;

    auto vstate = std::get<1>(vve::Renderer::GetState(m_registry));
    auto wstate = std::get<1>(vve::Window::GetState(m_registry, m_windowName));

    // Use swapChainExtent for physical pixel resolution instead of logical
    // window size (fixes High DPI / Retina scaling)
    VkExtent2D extent = vstate().m_swapChain.m_swapChainExtent;
    uint32_t imageSize = extent.width * extent.height * 4; // RGBA
    VkImage image =
        vstate().m_swapChain.m_swapChainImages[vstate().m_imageIndex];

    // Lazy-init on first frame (now we know the resolution)
    if (!m_initialized) {
      if (!InitEncoder(extent.width, extent.height))
        return false;
    }

    // Copy the swapchain image to host memory
    uint8_t *dataImage = new uint8_t[imageSize];
    vvh::ImgCopyImageToHost({
        vstate().m_device, vstate().m_vmaAllocator, vstate().m_graphicsQueue,
        vstate().m_commandPool, image, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, dataImage,
        extent.width, extent.height, imageSize, 2, 1, 0,
        3 // channel swap indices r,g,b,a
    });

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

    if (avcodec_send_frame(m_codecCtx, m_yuvFrame) < 0) {
      std::cerr << "Error sending frame to codec" << std::endl;
      av_frame_free(&rgbFrame);
      delete[] dataImage;
      return false;
    }

    if (!m_bitrateReduced && m_frameIndex >= c_bitrateSwitchFrame) {
      // Flush original codec context (Store pattern)
      avcodec_send_frame(m_codecCtx, nullptr);
      while (avcodec_receive_packet(m_codecCtx, m_pkt) == 0) {
        m_pkt->pts = m_pkt->dts =
            m_pkt->pts * m_codecCtx->time_base.den / m_codecCtx->time_base.num;
        m_outFile.write(reinterpret_cast<const char *>(m_pkt->data),
                        m_pkt->size);
        av_packet_unref(m_pkt);
      }

      avcodec_free_context(&m_codecCtx);
      m_codecCtx = avcodec_alloc_context3(m_codec);
      if (!m_codecCtx) {
        std::cerr
            << "FrameEncoder: Could not allocate replacement codec context"
            << std::endl;
      } else {
        m_codecCtx->bit_rate =
            m_bitrateVal / 2; // For dynamic testing if needed
        m_codecCtx->width = extent.width;
        m_codecCtx->height = extent.height;
        m_codecCtx->time_base = {1, c_fps};
        m_codecCtx->framerate = {c_fps, 1};
        m_codecCtx->gop_size = 10;
        m_codecCtx->max_b_frames = 1;
        m_codecCtx->pix_fmt = AV_PIX_FMT_YUV420P;

        if (avcodec_open2(m_codecCtx, m_codec, nullptr) < 0) {
          std::cerr
              << "FrameEncoder: Could not reopen codec after bitrate switch"
              << std::endl;
          avcodec_free_context(&m_codecCtx);
        } else {
          m_bitrateReduced = true;
          std::cout << "FrameEncoder: Switched bitrate to "
                    << (m_bitrateVal / 2) << " at frame " << m_frameIndex
                    << std::endl;
        }
      }
    }

    while (avcodec_receive_packet(m_codecCtx, m_pkt) == 0) {
      m_pkt->pts = m_pkt->dts = m_yuvFrame->pts * m_codecCtx->time_base.den /
                                m_codecCtx->time_base.num;
      m_outFile.write(reinterpret_cast<const char *>(m_pkt->data), m_pkt->size);
      av_packet_unref(m_pkt);
    }

    m_frameIndex++;
    av_frame_free(&rgbFrame);
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
      m_pkt->pts = m_pkt->dts =
          m_pkt->pts * m_codecCtx->time_base.den / m_codecCtx->time_base.num;
      m_outFile.write(reinterpret_cast<const char *>(m_pkt->data), m_pkt->size);
      av_packet_unref(m_pkt);
    }

    m_outFile.close();

    av_packet_free(&m_pkt);
    if (m_yuvFrame) {
      av_freep(&m_yuvFrame->data[0]);
      av_frame_free(&m_yuvFrame);
    }
    avcodec_free_context(&m_codecCtx);
    sws_freeContext(m_swsCtx);
    m_swsCtx = nullptr;

    std::string startFile = m_outFileBase + "." + m_codecName;
    std::cout << "FrameEncoder: encoded " << m_frameIndex << " frames to "
              << startFile << std::endl;

    // Convert elementary stream to MP4
    std::string endFile = m_outFileBase + ".mp4";
    std::string tagCmd = (m_codecName == "hevc" || m_codecName == "h265")
                             ? " -tag:v hvc1 "
                             : " ";
    std::string cmd = std::string("/opt/homebrew/bin/ffmpeg -y -i ") +
                      startFile + " -c copy" + tagCmd + endFile;
    std::cout << "FrameEncoder: running: " << cmd << std::endl;
    std::system(cmd.c_str());
    std::cout << "FrameEncoder: created " << endFile << std::endl;
  }
};