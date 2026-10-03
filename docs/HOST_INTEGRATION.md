# Host integration

Include `emuframe/emuframe.h` and link `EmuFrameCore.lib` on Windows. The host controls pacing, rendering, and audio playback. The DLL does not create a window or start its own worker thread.

```c
#include <emuframe/emuframe.h>
#include <stdlib.h>

int run_game(const char* user_selected_rom_path) {
    EF_Config config = {0};
    config.struct_size = sizeof(config);
    config.audio_enabled = 1;
    config.volume = 1.0f;
    EF_Handle emu = 0;
    if (ef_create_instance(EF_SYSTEM_AUTO, &config, &emu) != EF_OK) return 1;
    if (ef_load_rom(emu, user_selected_rom_path) != EF_OK) {
        ef_destroy_instance(emu);
        return 2;
    }
    EF_GameInfo game;
    ef_get_game_info(emu, &game); /* title, system, SHA-256, size, path */
    ef_start(emu);

    EF_InputState input = {0};
    input.buttons = EF_BUTTON_A;
    ef_set_input(emu, &input);
    ef_run_frame(emu); /* call at the native refresh rate */
    EF_VideoFrame frame;
    ef_get_video_info(emu, &frame);
    size_t bytes = 0;
    ef_copy_video(emu, NULL, 0, &bytes);
    void* pixels = malloc(bytes);
    if (pixels) {
        ef_copy_video(emu, pixels, bytes, NULL);
        /* Render frame.width × frame.height using frame.pitch and RGBA8888. */
        free(pixels);
    }
    EF_AudioInfo audio;
    ef_get_audio_info(emu, &audio);
    /* Read interleaved int16 PCM with ef_read_audio and enqueue it in a host mixer. */
    ef_save_state(emu, 0);
    ef_close_rom(emu); /* flushes cartridge save */
    ef_destroy_instance(emu);
    return 0;
}
```

All paths are UTF-8. A handle remains valid until `ef_destroy_instance`; using it afterward returns `EF_ERROR_INVALID_HANDLE`. `ef_get_last_error` retrieves a per-instance description after a failed operation. Video and audio copies are safe across calls because they use caller-owned buffers. Check `ef_get_video_info` after stepping, since dimensions may change. `ef_copy_video` reports the required byte count even when the supplied buffer is too small. Audio availability is in frames, with `channels` samples per frame. No exceptions cross the C boundary.

`EF_Config.frame_limit`, `prefer_bios`, and `integer_scaling` are reserved for future host/backend negotiation in v0.1. Hosts should pace `ef_run_frame` themselves; the test host follows audio playback when available and uses the native refresh rate otherwise. Query `ef_get_audio_info` for the output rate and channel count before opening a device; the mGBA backend currently provides 44.1 kHz stereo PCM. The supplied `volume` and `audio_enabled` settings affect audio returned by `ef_read_audio`.
Hosts can also call `ef_set_audio_options` to change volume or mute an existing instance. The test host reads `emuframe.ini` beside its EXE and can reload these values from its Settings menu.

`ef_scan_library` scans a directory recursively without changing files. `ef_index_library` merges results into a host-chosen database path and retains `last_played` and accumulated play seconds. `ef_record_library_play` updates those fields by SHA-256. These library calls are optional and synchronous.
