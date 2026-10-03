# EmuFrame 0.1

**In development:** EmuFrame is an early test framework, not a finished emulator release. It is a small C ABI around an mGBA backend for user-provided Game Boy, Game Boy Color, and Game Boy Advance ROMs. The framework owns emulation, input routing, video/audio buffers, save paths, and save states. Hosts render and play the output themselves.

EmuFrame is intended to support more emulator backends over time, including PlayStation and Xbox systems. Version 0.1 only supports GB, GBC, and GBA; the future systems will need their own backends and API extensions for media, firmware, controllers, and storage.

## Build (Windows x64)

Use CMake 3.20+ and a C++20 compiler. mGBA is pinned as a Git submodule at `Emulator Master Folders/mgba-master/` and built as a static dependency. Clone with `git clone --recurse-submodules https://github.com/AC9892/EmuFrameWork.git`, or run `git submodule update --init --recursive` after an ordinary clone. No ROMs or firmware are downloaded. If you keep mGBA elsewhere, set `-DEF_MGBA_SOURCE_DIR="<path to mGBA source>"` when configuring CMake.

```powershell
cmake -S . -B build -G "Visual Studio 16 2019" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Run `build/Release/EmuFrameTestHost.exe`. Open a `.gb`, `.gbc`, or `.gba` file through File > Open ROM. Use arrow keys for the D-pad, Z/X for A/B, Enter/Backspace for Start/Select, and A/S for L/R. The first connected XInput controller also works, regardless of its player slot. DualSense and other Windows joystick devices use a fallback mapping: D-pad or left stick, Cross/Circle for A/B, Create/Options for Select/Start, and shoulder buttons for L/R. Keyboard and controller input only reaches the game while the test host is focused. Emulation menu has pause, resume, reset, and save/load state slot 0. The Debug menu shows metadata, backend, performance, controller detection, and opens the log file.

On first launch, the host creates `emuframe.ini` beside the EXE. Use Settings > Open Settings File to edit it and Settings > Reload Settings to apply audio, pacing, scaling, and window changes. `data_directory` changes after restarting the host. An [example settings file](apps/test_host/emuframe.ini.example) lists every option. `volume` accepts 0–2, `audio_latency_ms` accepts 40–250 (default 120), and `max_audio_backlog_ms` accepts 40–500 (default 250). The mGBA backend uses mGBA's sinc resampler to provide 44.1 kHz stereo audio. The test host follows the audio device's playback position when audio is available and falls back to native-rate wall-clock pacing when it is not. Emulation and audio run on separate workers so moving the window or opening a menu does not stop the core. The log reports output underruns, sample drops, and runs of near-black frames produced by the backend. Logs are appended immediately to `EmuFrameData/logs/test-host.log` (under the configured data directory), and Debug > Open Log File opens them outside the small test window.

For repeatable local testing, the test host also accepts a ROM path as its sole command-line argument.

The build produces `EmuFrameCore.dll` and `EmuFrameCore.lib` next to the test host. The C header is `include/emuframe/emuframe.h`. Other hosts can link the import library or load the DLL dynamically.

For a minimal embedding check, double-click `build/Release/EmuFrameEmbedExample.exe` on Windows to choose a ROM, or run it with a ROM path from a terminal. Its [source](examples/minimal_host.cpp) uses only the public API to load a ROM and consume 120 frames of video and audio. It does not display or play them; see [host integration](docs/HOST_INTEGRATION.md) for how to connect those buffers to a game's renderer and mixer.

The test host uses Win32 and waveOut; the core uses standard C++ facilities and is intended to remain portable. The current build has been verified with the Visual Studio 2019 toolchain. The API and generated ROM tests pass, including independent simultaneous instances, battery-backed cartridge save persistence across close/reopen, and save-state loading after reopen. User testing has covered real-game playback and a DualSense controller, with no audio or display issue noticed in the latest session; black flashes in the OBS recording were not visible in the test host. More devices and games still need testing. A functional ROM session needs a ROM supplied by the user; no ROM is included here.

## Data and licensing

By default the test host writes data beside its EXE in `EmuFrameData/`. Other hosts can set `EF_Config.data_directory` to choose a location. Cartridge saves use `saves/<SHA-256>/game.sav`; states use `states/<SHA-256>/slot_N.state`. The ROM files are only read, never moved or modified. mGBA manages cartridge save flushing when the ROM closes. State writes use a temporary file and atomic replacement when the filesystem supports it.

mGBA is licensed under the Mozilla Public License 2.0; its source and license are in `Emulator Master Folders/mgba-master/` and its `LICENSE` file. Distributors of binaries containing mGBA must comply with MPL 2.0, including making the MPL-covered source and modifications available and preserving notices. EmuFrame does not include games, Nintendo BIOS dumps, or firmware.

See [architecture](docs/ARCHITECTURE.md), [host integration](docs/HOST_INTEGRATION.md), and [backend notes](docs/BACKENDS.md).

Optional local Game Boy, Game Boy Color, and Game Boy Advance models are not included in this public repository. [Model credits](docs/MODEL_CREDITS.md) link to the creators' original downloads and licenses. Some local copies have been modified, so the originals may look different. The Game Boy model uses CC BY-NC 4.0, the Game Boy Color model uses CC BY 4.0, and the GBA listing identifies an Editorial License (no AI). Check those terms before distributing the models.
