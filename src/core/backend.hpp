#pragma once
#include <emuframe/emuframe.h>
#include <filesystem>
#include <memory>
#include <vector>

namespace ef {
struct Backend {
  virtual ~Backend() = default;
  virtual bool load(const std::filesystem::path&, const std::filesystem::path&) = 0;
  virtual void reset() = 0;
  virtual void run_frame() = 0;
  virtual void set_input(uint32_t) = 0;
  virtual EF_VideoFrame video_info() const = 0;
  virtual const uint32_t* video_pixels() const = 0;
  virtual EF_AudioInfo audio_info() const = 0;
  virtual size_t read_audio(int16_t*, size_t) = 0;
  virtual bool save_state(const std::filesystem::path&) = 0;
  virtual bool load_state(const std::filesystem::path&) = 0;
  virtual const char* name() const = 0;
};
std::unique_ptr<Backend> create_mgba_backend();
}
