#pragma once
#include "backend.hpp"
#include "rom.hpp"
#include <chrono>
#include <mutex>
#include <string>
namespace ef {
class Instance {
public:
  explicit Instance(EF_System preferred, const EF_Config* config);
  EF_Result load(const char* path);
  EF_Result close();
  EF_Result start();
  EF_Result pause();
  EF_Result resume();
  EF_Result reset();
  EF_Result stop();
  EF_Result frame();
  EF_Result input(const EF_InputState* value);
  EF_Result state(uint32_t slot, bool save);
  void log(EF_LogLevel level, const std::string& message);
  EF_Result fail(EF_Result code, const std::string& message);
  std::mutex mutex;
  EF_System preferred;
  EF_Status status=EF_STATUS_EMPTY;
  std::filesystem::path data_dir;
  Rom rom;
  std::unique_ptr<Backend> backend;
  EF_Performance performance{};
  std::string last_error;
  EF_LogCallback logger=nullptr;
  void* logger_user=nullptr;
  bool audio_enabled=true;
  float volume=1.0f;
  std::chrono::steady_clock::time_point last_frame{};
  std::chrono::steady_clock::time_point fps_window_start{};
  uint32_t fps_window_frames=0;
};
}
