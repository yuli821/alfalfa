#include "raw_video_reader.hh"
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

RawVideoReader::RawVideoReader(const std::string & config_path)
  : pixel_format_(PixelFormat::YUV420),
  y4m_(false),
  file_(nullptr),
  width_(0),
  height_(0),
  y_size_(0),
  uv_size_(0),
  frame_delay_us_(0),
  frame_delay_(0),
  last_frame_time_(Clock::now()),
  reader_thread_(),      // match order
  running_(false),       // match order
  frame_queue_()
{
  load_config(config_path);
}

RawVideoReader::~RawVideoReader() {
  stop();
  if (file_) fclose(file_);
}

void RawVideoReader::load_config(const std::string & config_path) {
  using json = nlohmann::json;

  FILE * config_file = fopen(config_path.c_str(), "r");
  if (!config_file) throw std::runtime_error("Failed to open config file");

  fseek(config_file, 0, SEEK_END);
  size_t size = ftell(config_file);
  rewind(config_file);

  std::string buffer(size, '\0');
  size_t size2 = fread(&buffer[0], 1, size, config_file);
  fclose(config_file);
  (void)size2;
  assert(size2 == size && "Unmatched fread size");

  json config = json::parse(buffer);

  std::string video_path = config["video_path"];
  width_ = config["width"];
  height_ = config["height"];
  int fps = config.value("fps", 30);
  std::string pixfmt_str = config.value("pixfmt", "YU12");
  y4m_ = config.value("y4m", false);

  if (pixfmt_str == "YU12" || pixfmt_str == "YUV420") pixel_format_ = PixelFormat::YUV420;
  else if (pixfmt_str == "NV12") pixel_format_ = PixelFormat::NV12;
  else if (pixfmt_str == "YUYV") pixel_format_ = PixelFormat::YUYV;
  else throw std::runtime_error("Unsupported pixel format: " + pixfmt_str);

  file_ = fopen(video_path.c_str(), "rb");
  if (!file_) throw std::runtime_error("Failed to open video file");

  if (y4m_) {
    char header[1024];
    if (!fgets(header, sizeof(header), file_)) {
      throw std::runtime_error("Failed to read Y4M header");
    }
  }

  y_size_ = width_ * height_;
  uv_size_ = y_size_ / 4;
  frame_delay_us_ = 1000000 / fps;
  frame_delay_ = std::chrono::microseconds(frame_delay_us_);
  printf("fps: %d\n", fps);
  printf("frame_delay in microseconds: %d\n", frame_delay_us_);
}

void RawVideoReader::start() {
  running_ = true;
  reader_thread_ = std::thread(&RawVideoReader::frame_reader_loop, this);
}

void RawVideoReader::stop() {
  // running_ = false;
  // queue_cv_.notify_all();
  if (reader_thread_.joinable()) {
    reader_thread_.join();
  }
}

void RawVideoReader::frame_reader_loop() {
  // auto next_time = Clock::now();
  while (true) {
    // std::unique_lock<std::mutex> lock(queue_mutex_);
    // queue_cv_.wait(lock, [this]() {
      // return frame_queue_.size() < max_queue_size_;
    // });
    if (frame_queue_.write_available()) {
      auto frame = read_and_decode_one();
      if (!frame.initialized()) {
        running_ = false;
        break;
      }
      frame_queue_.push(std::move(frame.get()));
    }
    // next_time += frame_delay_;
    // frame_queue_.push(std::move(frame.get()));
    // lock.unlock();
    // queue_cv_.notify_all();
    const auto start = std::chrono::high_resolution_clock::now();
    std::this_thread::sleep_for(frame_delay_);
    const auto end = std::chrono::high_resolution_clock::now();
    const std::chrono::duration<double, std::milli> elapsed = end - start;
 
    // printf("Watied: %f\n", elapsed.count());
  }
}

Optional<RasterHandle> RawVideoReader::get_next_frame() {
  // std::unique_lock<std::mutex> lock(queue_mutex_);
  // queue_cv_.wait(lock, [this]() {
  //   return !frame_queue_.empty() || !running_;
  // });
  if (!running_) return {};
  if (frame_queue_.empty()) return {};

  Optional<RasterHandle> frame;
  if (frame_queue_.pop(frame)) {
    return frame;
  }
  return {};
}

Optional<RasterHandle> RawVideoReader::read_and_decode_one() {
  if (y4m_) {
    char frame_tag[10];
    if (!fgets(frame_tag, sizeof(frame_tag), file_)) return {};
    if (strncmp(frame_tag, "FRAME", 5) != 0) return {};
  }

  MutableRasterHandle raster_handle{width_, height_};
  auto & raster = raster_handle.get();

  switch (pixel_format_) {
    case PixelFormat::YUV420: {
      if (fread(&raster.Y().at(0, 0), 1, y_size_, file_) != y_size_) return {};
      if (fread(&raster.U().at(0, 0), 1, uv_size_, file_) != uv_size_) return {};
      if (fread(&raster.V().at(0, 0), 1, uv_size_, file_) != uv_size_) return {};
      break;
    }
    case PixelFormat::NV12: {
      if (fread(&raster.Y().at(0, 0), 1, y_size_, file_) != y_size_) return {};
      std::vector<uint8_t> uv_buf(2 * uv_size_);
      if (fread(uv_buf.data(), 1, uv_buf.size(), file_) != uv_buf.size()) return {};
      for (size_t i = 0; i < uv_size_; ++i) {
        size_t x = i % (width_ / 2), y = i / (width_ / 2);
        raster.U().at(x, y) = uv_buf[2 * i];
        raster.V().at(x, y) = uv_buf[2 * i + 1];
      }
      break;
    }
    case PixelFormat::YUYV: {
      size_t total = 2 * width_ * height_;
      std::vector<uint8_t> buf(total);
      if (fread(buf.data(), 1, total, file_) != total) return {};
      size_t y_idx = 0, uv_idx = 0;
      for (size_t i = 0; i < total; i += 4) {
        size_t x = (y_idx % width_), y = (y_idx / width_);
        raster.Y().at(x, y) = buf[i];
        raster.Y().at(x + 1, y) = buf[i + 2];
        y_idx += 2;
        if ((uv_idx / (width_ / 2)) % 2 == 0) {
          size_t ux = uv_idx % (width_ / 2), uy = uv_idx / (width_ / 2);
          raster.U().at(ux, uy) = buf[i + 1];
          raster.V().at(ux, uy) = buf[i + 3];
          uv_idx++;
        }
      }
      break;
    }
  }

  return RasterHandle{std::move(raster_handle)};
}
