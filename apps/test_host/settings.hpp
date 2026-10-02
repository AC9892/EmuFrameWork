#pragma once
#include <filesystem>
#include <string>

struct TestHostSettings {
  bool audio_enabled=true;
  float volume=1.0f;
  bool frame_limit=true;
  bool integer_scaling=true;
  bool preserve_aspect_ratio=true;
  int audio_latency_ms=120;
  int max_audio_backlog_ms=120;
  int window_width=760;
  int window_height=560;
  std::string data_directory="EmuFrameData";
};

std::filesystem::path settings_path();
TestHostSettings load_settings(const std::filesystem::path& path,std::string& warning);
