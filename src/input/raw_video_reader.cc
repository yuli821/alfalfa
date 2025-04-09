#include "raster.hh"
#include "optional.hh"
#include "frame_input.hh"
#include "raw_video_reader.hh"
#include <string>
#include <cstdio>
#include <nlohmann/json.hpp>
#include <unistd.h>
#include <functional>
#include <stdexcept>
#include <chrono>
#include <cassert>

RawVideoReader::RawVideoReader(const std::string & config_path):last_frame_time_(Clock::now()), frame_delay_(0) {
    using json = nlohmann::json;

    FILE * config_file = fopen(config_path.c_str(), "r");
    if (!config_file) {
      throw std::runtime_error("Failed to open config file");
    }

    fseek(config_file, 0, SEEK_END);
    size_t size = ftell(config_file);
    fseek(config_file, 0, SEEK_SET);

    std::string buffer(size, '\0');
    size_t size2 = fread(&buffer[0], 1, size, config_file);
    fclose(config_file);
    (void)size2;
    assert(size == size2 && "Unmatched fread size");

    json config = json::parse(buffer);

    std::string video_path = config["video_path"];
    width_ = config["width"];
    height_ = config["height"];
    int fps = config.value("fps", 30);
    std::string pixfmt_str = config.value("pixfmt", "YU12");
    y4m_ = config.value("y4m", false);
    
    if (pixfmt_str == "YU12" || pixfmt_str == "YUV420") {
      pixel_format_ = 0;
    } else if (pixfmt_str == "NV12") {
      pixel_format_ = 1;
    } else if (pixfmt_str == "YUYV") {
      pixel_format_ = 2;
    } else {
      throw std::runtime_error("Unsupported pixel format in config: " + pixfmt_str);
    }

    file_ = fopen(video_path.c_str(), "rb");
    if (!file_) {
      throw std::runtime_error("Failed to open video file");
    }
    if (y4m_) {
      // Skip Y4M header line
      char header_line[1024];
      if (!fgets(header_line, sizeof(header_line), file_)) {
        throw std::runtime_error("Failed to read Y4M header");
      }
    }

    y_size_ = width_ * height_;
    uv_size_ = y_size_ / 4;
    int frame_delay_us_ = 1000000 / fps;
    frame_delay_ = std::chrono::microseconds(frame_delay_us_);
}

RawVideoReader::~RawVideoReader() {
    if (file_) fclose(file_);
}

Optional<RasterHandle> RawVideoReader::get_next_frame() {
  auto now = Clock::now();
  auto elapsed = now - last_frame_time_;
  if (elapsed < frame_delay_) {
    std::this_thread::sleep_for(frame_delay_ - elapsed);
  }
  last_frame_time_ = Clock::now();

  if (y4m_) {
    // Skip Y4M frame marker
    char frame_line[10];
    if (!fgets(frame_line, sizeof(frame_line), file_)) {
      return {};
    }
    if (strncmp(frame_line, "FRAME", 5) != 0) {
      return {};
    }
  }
  
  MutableRasterHandle raster_handle { width_, height_ };
  auto & raster = raster_handle.get();

  switch (pixel_format_) {
    case 0: {
      size_t read_Y = fread(&raster.Y().at(0, 0), 1, y_size_, file_);
      size_t read_U = fread(&raster.U().at(0, 0), 1, uv_size_, file_);
      size_t read_V = fread(&raster.V().at(0, 0), 1, uv_size_, file_);
      if (read_Y != y_size_ || read_U != uv_size_ || read_V != uv_size_) return {};
      break;
    }
    case 1: {
      size_t read_Y = fread(&raster.Y().at(0, 0), 1, y_size_, file_);
      uint8_t* uv_buf = new uint8_t[2 * uv_size_];
      size_t read_uv = fread(uv_buf, 1, 2 * uv_size_, file_);
      if (read_Y != y_size_ || read_uv != 2 * uv_size_) { delete[] uv_buf; return {}; }
      for (size_t i = 0; i < uv_size_; ++i) {
        size_t x = i % (width_ / 2);
        size_t y = i / (width_ / 2);
        raster.U().at(x, y) = uv_buf[2 * i];
        raster.V().at(x, y) = uv_buf[2 * i + 1];
      }
      delete[] uv_buf;
      break;
    }
    case 2: {
      size_t yuyv_size = 2 * width_ * height_;
      uint8_t* yuyv_buf = new uint8_t[yuyv_size];
      size_t read = fread(yuyv_buf, 1, yuyv_size, file_);
      if (read != yuyv_size) { delete[] yuyv_buf; return {}; }
      size_t y_idx = 0, uv_idx = 0;
      for (size_t i = 0; i < yuyv_size; i += 4) {
        size_t x = (y_idx % width_);
        size_t y = (y_idx / width_);
        raster.Y().at(x, y) = yuyv_buf[i];
        y_idx++;
        raster.Y().at(x + 1, y) = yuyv_buf[i + 2];
        y_idx++;
        if ((uv_idx / (width_ / 2)) % 2 == 0) {
          size_t ux = uv_idx % (width_ / 2);
          size_t uy = uv_idx / (width_ / 2);
          raster.U().at(ux, uy) = yuyv_buf[i + 1];
          raster.V().at(ux, uy) = yuyv_buf[i + 3];
          uv_idx++;
        }
      }
      delete[] yuyv_buf;
      break;
    }
  }
  return RasterHandle{ std::move(raster_handle) };
}