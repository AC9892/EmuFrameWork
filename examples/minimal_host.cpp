#include <emuframe/emuframe.h>

#include <cstdint>
#include <cstdio>
#include <iterator>
#include <string>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#endif

namespace {
#ifdef _WIN32
std::string choose_rom() {
  wchar_t path[32768]{};
  OPENFILENAMEW dialog{};
  dialog.lStructSize = sizeof(dialog);
  dialog.lpstrFilter = L"Game Boy ROMs (*.gb;*.gbc;*.gba)\0*.gb;*.gbc;*.gba\0All files (*.*)\0*.*\0";
  dialog.lpstrFile = path;
  dialog.nMaxFile = static_cast<DWORD>(std::size(path));
  dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
  if (!GetOpenFileNameW(&dialog)) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
  if (size <= 1) return {};
  std::string result(static_cast<std::size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, path, -1, result.data(), size, nullptr, nullptr);
  result.pop_back();
  return result;
}
#endif
}

// A console consumer of the public C ABI. A real host would upload pixels to a
// texture, queue PCM to its audio device, and pace calls to ef_run_frame.
int main(int argc, char** argv) {
  if (argc > 2) {
    std::fprintf(stderr, "Usage: EmuFrameEmbedExample <path-to-your-rom>\n");
    return 2;
  }
  bool picked_rom = false;
  std::string rom_path;
  if (argc == 2) {
    rom_path = argv[1];
  } else {
#ifdef _WIN32
    rom_path = choose_rom();
    if (rom_path.empty()) return 0; // The file picker was cancelled.
    picked_rom = true;
#else
    std::fprintf(stderr, "Usage: EmuFrameEmbedExample <path-to-your-rom>\n");
    return 2;
#endif
  }
  if (ef_api_version() != EF_API_VERSION) {
    std::fprintf(stderr, "Unsupported EmuFrame API version\n");
    return 1;
  }

  EF_Config config{};
  config.struct_size = sizeof(config);
  config.audio_enabled = 1;
  config.volume = 1.0f;
  EF_Handle emu = 0;
  EF_Result result = ef_create_instance(EF_SYSTEM_AUTO, &config, &emu);
  if (result != EF_OK) {
    std::fprintf(stderr, "Create instance: %s\n", ef_result_string(result));
    return 1;
  }

  int exit_code = 1;
  std::vector<std::uint8_t> pixels;
  std::vector<std::int16_t> pcm;
  std::uint64_t audio_frames = 0;
  EF_VideoFrame last_video{};
  auto check = [&](const char* operation, EF_Result status) {
    if (status == EF_OK) return true;
    char detail[256]{};
    ef_get_last_error(emu, detail, sizeof(detail));
    std::fprintf(stderr, "%s: %s%s%s\n", operation, ef_result_string(status),
                 detail[0] ? " - " : "", detail);
    return false;
  };

  do {
    if (!check("Load ROM", ef_load_rom(emu, rom_path.c_str()))) break;
    EF_GameInfo game{};
    if (!check("Get game info", ef_get_game_info(emu, &game))) break;
    std::printf("%s (system %d, SHA-256 %s)\n", game.title,
                static_cast<int>(game.system), game.sha256);
    if (!check("Start", ef_start(emu))) break;

    EF_InputState input{}; // Replace with input collected by the host each frame.
    bool failed = false;
    for (int i = 0; i < 120; ++i) {
      if (!check("Set input", ef_set_input(emu, &input)) ||
          !check("Run frame", ef_run_frame(emu))) {
        failed = true;
        break;
      }

      EF_VideoFrame video{};
      if (!check("Get video info", ef_get_video_info(emu, &video))) {
        failed = true;
        break;
      }
      last_video = video;
      pixels.resize(static_cast<std::size_t>(video.pitch) * video.height);
      if (!check("Copy video", ef_copy_video(emu, pixels.data(), pixels.size(), nullptr))) {
        failed = true;
        break;
      }
      // Submit pixels.data() to the host renderer using width, height, and pitch.

      EF_AudioInfo audio{};
      if (!check("Get audio info", ef_get_audio_info(emu, &audio))) {
        failed = true;
        break;
      }
      pcm.resize(audio.available_frames * audio.channels);
      std::size_t received = 0;
      if (audio.available_frames &&
          !check("Read audio", ef_read_audio(emu, pcm.data(), audio.available_frames, &received))) {
        failed = true;
        break;
      }
      audio_frames += received;
      // Submit received interleaved PCM frames at audio.sample_rate to the host mixer.
    }
    if (failed) break;
    std::printf("Read 120 video frames (%ux%u, pitch %u) and %llu audio frames\n",
                last_video.width, last_video.height, last_video.pitch,
                static_cast<unsigned long long>(audio_frames));
    if (!check("Close ROM", ef_close_rom(emu))) break;
    exit_code = 0;
  } while (false);

  ef_destroy_instance(emu);
  if (picked_rom) {
    std::puts("Press Enter to close this integration check.");
    std::getchar();
  }
  return exit_code;
}
