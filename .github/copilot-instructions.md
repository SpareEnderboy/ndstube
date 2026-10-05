# Workspace Setup

- [x] Verify `.github/copilot-instructions.md` exists.
- [x] Clarify project requirements: Nintendo DSi homebrew client with an Arch Linux LAN relay.
- [x] Scaffold the client and relay starter.
- [x] Customize the starter for relay-backed YouTube search and video playback.
- [x] Install required extensions: skipped; none are required by this project.
- [x] Compile the ROM with the installed `/opt/devkitpro` toolchain; output is `ndstube.nds`.
- [x] Create and run task: skipped; the project uses its root `Makefile` directly.
- [ ] Launch in melonDS: wait for user confirmation and a built ROM.
- [x] Ensure documentation is complete: README and this file verified.

## Project Notes

- The handheld client is C with libnds and DSWifi. It uses plain HTTP to a trusted LAN host; do not add TLS or YouTube extraction to the DS binary.
- The ROM includes TWL startup sections; request `setCpuClock(true)` only when `isDSiMode()` is true. Extra RAM/clock require launching in DSi mode.
- The Python relay owns YouTube search and stream resolution through yt-dlp. Keep API rows tab-separated and sanitize tabs/newlines in displayed fields.
- `/video?id=...` uses FFmpeg to stream bounded 128x96 RGB8 frames at 6 fps interleaved with 8 kHz signed mono PCM audio for at most 120 seconds. Do not imply arbitrary YouTube formats play natively on the DSi.
- Run relay tests with `python -m unittest discover -s service -v`.
- Do not claim that the DSi can play arbitrary YouTube streams. DSi decoding and Wi-Fi bandwidth are constrained; playback needs a separate compatible media pipeline.