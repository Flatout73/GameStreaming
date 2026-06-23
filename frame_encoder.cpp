#include <algorithm>
#include <chrono>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include "udp_sender6.h"

// FrameEncoder – captures every rendered frame, encodes it to H.264/HEVC,
// writes the elementary stream to a file, and sends each encoded packet
// over IPv6 UDP to a configurable receiver.
class FrameEncoder : public vve::System {
public:
  FrameEncoder(vve::Engine &engine, std::string windowName = "")
      : vve::System("FrameEncoder", engine),
        m_windowName(std::move(windowName)) {

    // --- Read configuration from Environment Variables ---
    const char *envCodec = std::getenv("ENC_CODEC");
    const char *envBitrate = std::getenv("ENC_BITRATE");
    const char *envOut = std::getenv("ENC_OUT");
    const char *envUdpAddr = std::getenv("UDP_ADDR");
    const char *envUdpPort = std::getenv("UDP_PORT");

    m_codecName = envCodec ? envCodec : "hevc";
    m_bitrateVal = envBitrate ? std::stoll(envBitrate) : 500000;
    m_outFileBase = envOut ? envOut : "output";
    m_udpAddr = envUdpAddr ? envUdpAddr : "::1";
    m_udpPort = envUdpPort ? std::stoi(envUdpPort) : 50000;

    // Frame duration for rate limiting (mutable; can be reduced on loss)
    m_currentFps = c_target_fps;
    m_frameDuration =
        std::chrono::nanoseconds(std::chrono::seconds(1)) / m_currentFps;

    // Find the encoder
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

    // --- Initialize UDP sender ---
    m_udpSender.init(m_udpAddr.c_str(), m_udpPort);

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

  // ---- Pre-allocated readback buffer (avoid per-frame heap alloc) ----
  uint8_t *m_readbackBuffer{nullptr};
  uint32_t m_readbackBufferSize{0};
  int64_t m_frameIndex{0};
  std::string m_windowName;
  std::string m_codecName;
  std::string m_outFileBase;
  int64_t m_bitrateVal;

  // ---- UDP streaming state ----
  UDPSender6 m_udpSender;
  std::string m_udpAddr;
  int m_udpPort{50000};

  // ---- Frame rate limiting ----
  static constexpr int c_target_fps = 30;
  static constexpr int c_min_fps = 10;
  int m_currentFps{c_target_fps};
  std::chrono::steady_clock::time_point m_lastFrameTime{};
  std::chrono::nanoseconds m_frameDuration{};
  bool m_firstFrame{true};

  // ---- Receiver-report polling ----
  std::chrono::steady_clock::time_point m_lastReportPoll{};

  static constexpr int c_fps = 30;

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
    m_codecCtx->max_b_frames = 0; // no B-frames -> no encode/decode reorder delay
    m_codecCtx->pix_fmt = AV_PIX_FMT_YUV420P;

    // Low latency: no B-frames, no rate-control lookahead, no frame-threading
    // (each adds a multi-frame buffer before the first packet is emitted).
    av_opt_set(m_codecCtx->priv_data, "preset", "ultrafast", 0);
    if (m_codecName == "hevc" || m_codecName == "h265") {
      av_opt_set(m_codecCtx->priv_data, "x265-params",
                 "bframes=0:rc-lookahead=0:sync-lookahead=0:frame-threads=1", 0);
    } else {
      // x264: zerolatency tune disables B-frames and lookahead.
      av_opt_set(m_codecCtx->priv_data, "tune", "zerolatency", 0);
    }

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
              << " → streaming to [" << m_udpAddr << "]:" << m_udpPort << " @ "
              << c_target_fps << " fps" << std::endl;
    return true;
  }

  bool OnFrameEnd(Message message) {
    if (!m_codec)
      return false;

    // ---- Frame rate limiting using std::chrono ----
    auto now = std::chrono::steady_clock::now();
    if (m_firstFrame) {
      m_lastFrameTime = now;
      m_firstFrame = false;
    } else {
      auto elapsed = now - m_lastFrameTime;
      if (elapsed < m_frameDuration) {
        return false; // Skip this frame — too soon
      }
    }
    m_lastFrameTime = now;

    // ---- Poll for a receiver report on the UDP socket ----
    // Check at most a few times per second to avoid syscall overhead.
    if (m_lastReportPoll == std::chrono::steady_clock::time_point{} ||
        (now - m_lastReportPoll) > std::chrono::milliseconds(500)) {
      m_lastReportPoll = now;
      ReceiverReport report{};
      if (m_udpSender.pollReceiverReport(report)) {
        std::cout << "FrameEncoder: receiver report"
                  << " byteRate=" << report.receivedByteRate
                  << " loss=" << report.packetLossRate
                  << " fps=" << report.frameRate << std::endl;
        // If the receiver lost packets, throttle the source frame rate a bit.
        // 1% loss -> drop 2 fps
        if (report.packetLossRate > 0.01 && m_currentFps > c_min_fps) {
          int newFps = std::max(c_min_fps, m_currentFps - 2);
          if (newFps != m_currentFps) {
            m_currentFps = newFps;
            m_frameDuration =
                std::chrono::nanoseconds(std::chrono::seconds(1)) /
                m_currentFps;
            std::cout << "FrameEncoder: reducing send rate to " << m_currentFps
                      << " fps (loss " << report.packetLossRate << ")"
                      << std::endl;
          }
        }
      }
    }

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
    // Reuse pre-allocated buffer (avoid ~2.8MB alloc per frame)
    if (m_readbackBufferSize < imageSize) {
      delete[] m_readbackBuffer;
      m_readbackBuffer = new uint8_t[imageSize];
      m_readbackBufferSize = imageSize;
    }
    vvh::ImgCopyImageToHost({
        vstate().m_device, vstate().m_vmaAllocator, vstate().m_graphicsQueue,
        vstate().m_commandPool, image, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        m_readbackBuffer, extent.width, extent.height, imageSize, 2, 1, 0,
        3 // channel swap indices r,g,b,a
    });

    AVFrame *rgbFrame = av_frame_alloc();
    rgbFrame->format = AV_PIX_FMT_RGBA;
    rgbFrame->width = extent.width;
    rgbFrame->height = extent.height;

    // Wire up the rgbFrame's data array to point to our captured pixels
    av_image_fill_arrays(rgbFrame->data, rgbFrame->linesize, m_readbackBuffer,
                         AV_PIX_FMT_RGBA, extent.width, extent.height, 1);

    // Convert RGBA AVFrame -> YUV420P AVFrame (Store pattern)
    sws_scale(m_swsCtx, rgbFrame->data, rgbFrame->linesize, 0, extent.height,
              m_yuvFrame->data, m_yuvFrame->linesize);

    m_yuvFrame->pts = m_frameIndex; // Store pattern uses raw index for YUV pts

    if (avcodec_send_frame(m_codecCtx, m_yuvFrame) < 0) {
      std::cerr << "Error sending frame to codec" << std::endl;
      av_frame_free(&rgbFrame);
      return false;
    }


    while (avcodec_receive_packet(m_codecCtx, m_pkt) == 0) {
      m_pkt->pts = m_pkt->dts = m_yuvFrame->pts * m_codecCtx->time_base.den /
                                m_codecCtx->time_base.num;
      // Write to file
      m_outFile.write(reinterpret_cast<const char *>(m_pkt->data), m_pkt->size);
      // Send via UDP
      m_udpSender.send(reinterpret_cast<const char *>(m_pkt->data),
                       m_pkt->size);
      av_packet_unref(m_pkt);
    }

    m_frameIndex++;
    av_frame_free(&rgbFrame);
    return false;
  }

  // ---- Flush encoder, close file, convert to MP4, close UDP ----
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
      m_udpSender.send(reinterpret_cast<const char *>(m_pkt->data),
                       m_pkt->size);
      av_packet_unref(m_pkt);
    }

    m_outFile.close();

    // Close UDP socket
    m_udpSender.closeSock();

    av_packet_free(&m_pkt);
    if (m_yuvFrame) {
      av_freep(&m_yuvFrame->data[0]);
      av_frame_free(&m_yuvFrame);
    }
    delete[] m_readbackBuffer;
    m_readbackBuffer = nullptr;
    m_readbackBufferSize = 0;
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
                      startFile + " -c:v copy -bsf:v dts2pts -c:a copy" +
                      tagCmd + endFile;
    std::cout << "FrameEncoder: running: " << cmd << std::endl;
    std::system(cmd.c_str());
    std::cout << "FrameEncoder: created " << endFile << std::endl;
  }
};