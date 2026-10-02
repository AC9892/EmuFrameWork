#ifndef EMUFRAME_H
#define EMUFRAME_H
#include <stddef.h>
#include <stdint.h>
#if defined(_WIN32)
# if defined(EMUFRAME_BUILD)
#  define EF_API __declspec(dllexport)
# else
#  define EF_API __declspec(dllimport)
# endif
#else
# define EF_API __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif

#define EF_API_VERSION 1u
#define EF_MAX_TITLE 64
#define EF_MAX_PATH 1024
#define EF_SHA256_HEX 65
typedef uint64_t EF_Handle;
typedef enum EF_Result {
  EF_OK=0, EF_ERROR_INVALID_HANDLE, EF_ERROR_INVALID_ARGUMENT,
  EF_ERROR_FILE_NOT_FOUND, EF_ERROR_INVALID_ROM, EF_ERROR_UNSUPPORTED_SYSTEM,
  EF_ERROR_BACKEND_FAILURE, EF_ERROR_SAVE_FAILED, EF_ERROR_BAD_STATE,
  EF_ERROR_BUFFER_TOO_SMALL, EF_ERROR_IO
} EF_Result;
typedef enum EF_System { EF_SYSTEM_AUTO=0, EF_SYSTEM_GB=1, EF_SYSTEM_GBC=2, EF_SYSTEM_GBA=3 } EF_System;
typedef enum EF_Status { EF_STATUS_EMPTY=0, EF_STATUS_PAUSED=1, EF_STATUS_RUNNING=2 } EF_Status;
typedef enum EF_PixelFormat { EF_PIXEL_RGBA8888=1 } EF_PixelFormat;
typedef enum EF_LogLevel { EF_LOG_TRACE, EF_LOG_DEBUG, EF_LOG_INFO, EF_LOG_WARN, EF_LOG_ERROR, EF_LOG_FATAL } EF_LogLevel;
typedef void (*EF_LogCallback)(EF_LogLevel level, const char* message, void* user);

/* New fields may be appended in a future ABI version. Pass sizeof(EF_Config). */
typedef struct EF_Config {
  uint32_t struct_size;
  uint8_t audio_enabled;
  uint8_t frame_limit;
  uint8_t prefer_bios;
  uint8_t integer_scaling;
  float volume;
  const char* data_directory; /* UTF-8; NULL uses ./EmuFrameData */
} EF_Config;
typedef struct EF_InputState {
  uint32_t buttons; /* EF_BUTTON_* bits */
  float left_x, left_y, right_x, right_y; /* reserved for future backends */
  float left_trigger, right_trigger;
} EF_InputState;
enum {
  EF_BUTTON_A=1u<<0, EF_BUTTON_B=1u<<1, EF_BUTTON_SELECT=1u<<2,
  EF_BUTTON_START=1u<<3, EF_BUTTON_RIGHT=1u<<4, EF_BUTTON_LEFT=1u<<5,
  EF_BUTTON_UP=1u<<6, EF_BUTTON_DOWN=1u<<7, EF_BUTTON_R=1u<<8, EF_BUTTON_L=1u<<9
};
typedef struct EF_GameInfo {
  EF_System system;
  uint64_t rom_size;
  char title[EF_MAX_TITLE];
  char sha256[EF_SHA256_HEX];
  char loaded_path[EF_MAX_PATH];
  char save_type[32];
} EF_GameInfo;
typedef struct EF_VideoFrame {
  uint32_t width, height, pitch;
  EF_PixelFormat format;
  uint64_t sequence;
} EF_VideoFrame;
typedef struct EF_AudioInfo {
  uint32_t sample_rate, channels;
  size_t available_frames;
} EF_AudioInfo;
typedef struct EF_Performance {
  double emulated_fps, host_frame_ms, emulation_frame_ms;
  uint64_t frames, dropped_frames;
  size_t audio_buffer_frames;
} EF_Performance;
typedef void (*EF_LibraryCallback)(const EF_GameInfo* info, void* user);

EF_API uint32_t ef_api_version(void);
EF_API EF_Result ef_create_instance(EF_System preferred, const EF_Config* config, EF_Handle* out_handle);
EF_API void ef_destroy_instance(EF_Handle handle);
EF_API EF_Result ef_load_rom(EF_Handle handle, const char* utf8_path);
EF_API EF_Result ef_close_rom(EF_Handle handle);
EF_API EF_Result ef_start(EF_Handle handle);
EF_API EF_Result ef_pause(EF_Handle handle);
EF_API EF_Result ef_resume(EF_Handle handle);
EF_API EF_Result ef_reset(EF_Handle handle);
EF_API EF_Result ef_stop(EF_Handle handle);
EF_API EF_Result ef_run_frame(EF_Handle handle);
EF_API EF_Result ef_set_input(EF_Handle handle, const EF_InputState* input);
EF_API EF_Result ef_get_status(EF_Handle handle, EF_Status* out_status);
EF_API EF_Result ef_get_game_info(EF_Handle handle, EF_GameInfo* out_info);
EF_API EF_Result ef_get_video_info(EF_Handle handle, EF_VideoFrame* out_info);
EF_API EF_Result ef_copy_video(EF_Handle handle, void* rgba, size_t bytes, size_t* required);
EF_API EF_Result ef_get_audio_info(EF_Handle handle, EF_AudioInfo* out_info);
EF_API EF_Result ef_set_audio_options(EF_Handle handle, uint8_t enabled, float volume);
/* Interleaved signed 16-bit PCM. Reads at most capacity_frames. */
EF_API EF_Result ef_read_audio(EF_Handle handle, int16_t* output, size_t capacity_frames, size_t* written_frames);
EF_API EF_Result ef_save_state(EF_Handle handle, uint32_t slot);
EF_API EF_Result ef_load_state(EF_Handle handle, uint32_t slot);
EF_API EF_Result ef_get_performance(EF_Handle handle, EF_Performance* out_info);
EF_API EF_Result ef_set_log_callback(EF_Handle handle, EF_LogCallback callback, void* user);
EF_API EF_Result ef_get_last_error(EF_Handle handle, char* output, size_t capacity);
EF_API const char* ef_result_string(EF_Result result);
EF_API const char* ef_backend_name(void);
/* Optional synchronous scan; skips invalid files. Paths and metadata are read only. */
EF_API EF_Result ef_scan_library(const char* utf8_directory, EF_LibraryCallback callback, void* user, size_t* found);
/* Adds/updates scanned ROMs in a small text database while retaining play history. */
EF_API EF_Result ef_index_library(const char* utf8_directory, const char* utf8_database, size_t* found);
EF_API EF_Result ef_record_library_play(const char* utf8_database, const char* sha256, uint64_t seconds);

#ifdef __cplusplus
}
#endif
#endif
