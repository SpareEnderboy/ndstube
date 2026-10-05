# ndstube

`ndstube` is a Nintendo DS/DSi homebrew YouTube client backed by an Arch Linux relay. The client searches YouTube, lists results, and plays a low-resolution video with mono audio over local Wi-Fi. The relay uses yt-dlp to resolve separate audio/video streams and FFmpeg to convert them into RGB8 frames and signed PCM samples for libnds playback.

Playback is intentionally limited to 128x96 pixels, 6 frames per second, 8 kHz mono audio, and the first 120 seconds. Audio and video are interleaved in each frame interval over one connection. The raw stream is about 654 kbps before network overhead, so performance depends on the DSi's Wi-Fi link. DSi hardware has tight CPU, memory, and Wi-Fi limits; this does not make arbitrary YouTube streams directly playable on the console.

## Requirements

- Arch Linux host with `nds-dev`, `make`, and Python 3 installed.
- melonDS for emulator testing.
- A DSi or emulator network configuration that can reach the Arch host over the same LAN.

Install the host packages if needed:

```sh
sudo pacman -S --needed devkitARM libnds make python ffmpeg
yay -S melonds
```

Arch repositories can package devkitPro under different names; this workspace's installed toolchain provides `devkitARM` and `libnds` under `/opt/devkitpro`.

The relay uses yt-dlp in an isolated virtual environment:

```sh
python -m venv .venv
source .venv/bin/activate
python -m pip install -r service/requirements.txt
```

## Run The Relay

Start the service from the repository root:

```sh
python service/server.py
```

It listens on `0.0.0.0:8080` by default. Check the host's LAN address with `ip -brief address`, then verify the relay from the host:

```sh
curl http://127.0.0.1:8080/health
curl --get --data-urlencode 'q=DSi homebrew' http://127.0.0.1:8080/search
```

Set `NDSTUBE_PORT` to change the port. Allow inbound TCP on that port only from the trusted LAN if the host firewall is enabled. The `/video?id=VIDEO_ID` endpoint returns a bounded interleaved stream: one raw RGB8 frame followed by the corresponding 8 kHz signed mono PCM block. It requires yt-dlp and the system `ffmpeg` executable.

## Build The Client

The Makefile defaults to `/opt/devkitpro` and `/opt/devkitpro/devkitARM`. For a custom installation, set the paths before building:

```sh
export DEVKITPRO=/custom/path/devkitpro
export DEVKITARM="$DEVKITPRO/devkitARM"
make
```

Edit `SERVER_IP` in `client/source/main.c` to the Arch host's LAN IPv4 address before building. The client uses plain HTTP on the LAN and has no TLS or credentials. Do not expose the relay port to the public internet.

The Makefile uses the installed `devkitARM/ds_rules`, which packages the TWL ARM9i/ARM7i startup sections and DSi header. The DSi linker map provides a separate expanded-memory heap while retaining a DS-safe limit for the NTR sections. The expected output is `ndstube.nds`.

## Run In melonDS

Boot melonDS in DSi mode, not DS compatibility mode, to request the DSi's 134.06 MHz ARM9 clock and use its expanded RAM heap. The client reports the detected mode at startup; standard DS mode cannot provide the clock or extra RAM. Configure emulated DSi Wi-Fi to reach the same LAN as the host, then use Up/Down to choose a query character, A to append it, B to delete, and START to search. Use Left/Right to select a result and X to watch it with audio; B stops playback, and the DSi Home/exit event returns from the client. Search and playback are synchronous, so the UI waits while Wi-Fi and the relay respond.

If the emulator build cannot bridge DS Wi-Fi to the host LAN, test the ROM on a DSi with a configured access point or use a melonDS build/network mode that supports the required connectivity. The relay and client must be mutually reachable on TCP port 8080.

## Tests

Run the relay's offline unit tests without YouTube access:

```sh
python -m unittest discover -s service -v
```