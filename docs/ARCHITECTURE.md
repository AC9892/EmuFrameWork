# Architecture

`include/emuframe/emuframe.h` is the stable C-facing surface. `src/api` validates handles and catches C++ exceptions before they reach the DLL boundary. Each handle maps to one `Instance`, which owns a backend, ROM identity, status, configuration, error string, and performance counters. A per-instance mutex serializes operations on that instance; separate instances have separate backend state. `ef_run_frame` remains synchronous; the test host calls it from a dedicated worker.

`src/core` handles lifecycle, ROM header detection, SHA-256, save locations, errors, and logging. `backends/mgba` is the only module that includes mGBA headers. The internal `Backend` interface isolates the current emulator implementation. A future backend may run in process or through IPC, but supporting a different console may also require versioned additions to the public C API.

The renderer-neutral frame is native resolution (160×144 for GB/GBC, 240×160 for GBA) with RGBA8888 pixels. The backing buffer has a 256-pixel stride and is reused. `ef_copy_video` copies under the instance lock to keep pointers out of the ABI. The test host scales uniformly to preserve aspect ratio. Audio is interleaved signed 16-bit PCM and is pulled by the host. The mGBA backend uses mGBA's sinc resampler to expose 44.1 kHz output. The framework never opens an audio device.

ROM inspection checks known header signatures before loading, then mGBA performs its own validation. SHA-256 names save and state directories. Cartridge data is handled by mGBA's save VFile and flushed by its lifecycle. Save state files are separate and exclude cartridge save data. The optional library scanner reads supported files and can persist a small `EmuFrameLibraryV1` text index with per-game play history.

The current API is synchronous. Calls on one handle are serialized, but a callback invoked by EmuFrame must not call back into the same handle. Host code should run frame stepping on its own thread if it needs a separate emulation thread, and hand frame/audio copies to its renderer and mixer.

## Future systems

PlayStation and Xbox support is a design goal, not a feature of version 0.1. The current `ef_load_rom`/`ef_close_rom` names, cartridge-oriented metadata and scanner, GB button mask, and mGBA save handling describe the present handheld backend. Disc or installed-game media, firmware requirements, controller layouts, memory cards or other persistent storage, and different timing and video modes will need explicit contracts. Extend the ABI with versioned functions and structures when those backends are implemented so existing GB/GBC/GBA hosts can continue to use version 1.
