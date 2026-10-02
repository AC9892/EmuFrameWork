# Backends

`Backend` is a C++ interface internal to EmuFrame. It exposes load, reset, frame stepping, input, video, audio, and state operations. Core code only sees generic structs and never mGBA types. `create_mgba_backend()` currently creates the single registered implementation. System selection is restricted to GB, GBC, and GBA in the public API.

The mGBA adapter uses `mCoreFind` to validate a ROM and choose mGBA's GB or GBA core. GBC is handled by mGBA's GB core according to the cartridge header. Key bit positions match mGBA's A/B/Select/Start/Right/Left/Up/Down/R/L mask. The adapter uses the software framebuffer and mGBA's audio buffer. It passes a per-hash cartridge save path to `mCoreLoadSaveFile` and uses named VFiles for save states.

To add a backend later, implement or adapt the internal interface and add a factory/registration rule. Backends with different media, firmware, input, or storage needs will also require versioned public API extensions; the current ROM and GB input contract alone is insufficient. External process backends can copy frames and audio into the generic output contract without exposing process handles to hosts.
