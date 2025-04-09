// raw_video_reader.hh
#pragma once

#include "raster.hh"
#include "optional.hh"
#include "frame_input.hh"
#include <string>
#include <cstdio>
#include <nlohmann/json.hpp>
#include <unistd.h>
#include <thread>
#include <atomic>
#include <unordered_set>
#include <chrono>

using namespace std;

class RawVideoReader : public FrameInput {
  using FrameCallback = std::function<void(RasterHandle&&)>;
  using Clock = std::chrono::steady_clock;
public:
  RawVideoReader(const std::string & config_path);

  ~RawVideoReader();

  Optional<RasterHandle> get_next_frame() override;
  
  uint16_t display_width() { return width_; }
  uint16_t display_height() { return height_; }

  RawVideoReader(const RawVideoReader&) = delete;
  RawVideoReader& operator=(const RawVideoReader&) = delete;

private:
  FILE * file_ = nullptr;
  uint16_t width_ = 0;
  uint16_t height_ = 0;
  size_t y_size_ = 0;
  size_t uv_size_ = 0;
  size_t frame_size_ = 0;
  uint32_t pixel_format_ = 0;
  bool y4m_ = false;
  Clock::time_point last_frame_time_;
  std::chrono::microseconds frame_delay_;
};
