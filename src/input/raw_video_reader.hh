// raw_video_reader.hh
#pragma once

#include "raster_handle.hh"
#include "optional.hh"
#include <string>
#include <cstdio>
#include <nlohmann/json.hpp>
#include <unistd.h>
#include <thread>
#include <atomic>
#include <functional>
#include <stdexcept>
#include <chrono>
#include <cstring>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <boost/lockfree/spsc_queue.hpp>

class RawVideoReader {
public:
  using FrameCallback = std::function<void(RasterHandle&&)>;
  using Clock = std::chrono::steady_clock;

  explicit RawVideoReader(const std::string & config_path);
  ~RawVideoReader();

  Optional<RasterHandle> get_next_frame();
  void start();
  void stop();

  uint16_t display_width() { return width_; }
  uint16_t display_height() { return height_; }

  RawVideoReader(const RawVideoReader&) = delete;
  RawVideoReader& operator=(const RawVideoReader&) = delete;
  RawVideoReader(RawVideoReader&&) = delete;
  RawVideoReader& operator=(RawVideoReader&&) = delete;

private:
  enum class PixelFormat { YUV420, NV12, YUYV };

  void load_config(const std::string & config_path);
  void frame_reader_loop();
  Optional<RasterHandle> read_and_decode_one();

  PixelFormat pixel_format_;
  bool y4m_ = false;
  FILE * file_ = nullptr;
  uint16_t width_ = 0;
  uint16_t height_ = 0;
  size_t y_size_ = 0;
  size_t uv_size_ = 0;
  int frame_delay_us_ = 0;
  std::chrono::microseconds frame_delay_;
  Clock::time_point last_frame_time_;

  std::thread reader_thread_;
  std::atomic<bool> running_{false};

  // std::queue<RasterHandle> frame_queue_;
  static constexpr size_t max_queue_size_ = 1;
  boost::lockfree::spsc_queue<Optional<RasterHandle>, boost::lockfree::capacity<8>> frame_queue_;
};