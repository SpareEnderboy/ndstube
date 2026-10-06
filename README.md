# ndstube

`ndstube` is a Nintendo DS/DSi homebrew YouTube client backed by an Arch Linux relay. The client searches YouTube, lists results, and plays a low-resolution video with mono audio over local Wi-Fi. The relay uses yt-dlp to resolve separate audio/video streams and FFmpeg to convert them into RGB8 frames and signed PCM samples for libnds playback.

Playback is intentionally limited to 128x96 pixels, 6 frames per second, and 8 kHz mono audio. Audio and video are interleaved in each frame interval over one connection, and the relay no longer truncates the stream to a 120-second cap. The raw stream is about 654 kbps before network overhead for a 120-second example, so longer videos depend on the DSi's Wi-Fi link and CPU headroom. DSi hardware has tight CPU, memory, and Wi-Fi limits; this does not make arbitrary YouTube streams directly playable on the console.

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

### FastVideoDS SD Handoff

The bottom-screen `GET FV` action requests `/fastvideo?id=VIDEO_ID`. The relay downloads the selected video without the old 120-second cap, runs the separate FastVideoDS encoder, and caches the resulting `.fv` file under `~/.cache/ndstube/fastvideo`. The client streams it to `sd:/testVideo.fv`; after it reports completion, launch FastVideoDS Player from the DSi menu to play that file. This is an SD-card handoff, not in-app FastVideoDS playback.

Build the upstream encoder on an x86-64 host with AVX2 and the .NET SDK, following [FastVideoDSEncoder](https://github.com/Gericom/FastVideoDSEncoder). Its FFmpeg.AutoGen 5.1 binding requires FFmpeg 5.1 shared libraries; the encoder's published `x64` folder only contains Windows DLLs. With micromamba installed, create an isolated compatible runtime and publish the encoder to a persistent user-local directory:

```sh
"$HOME/.local/bin/micromamba" create -y -p "$HOME/.local/share/ndstube/ffmpeg51" -c conda-forge 'ffmpeg=5.1.2'
git clone https://github.com/Gericom/FastVideoDSEncoder.git
cd FastVideoDSEncoder
encoder_dir="$HOME/.local/share/ndstube/FastVideoDSEncoder"
dotnet publish FastVideoDSEncoder/FastVideoDSEncoder.csproj -c Release -r linux-x64 --self-contained false -o "$encoder_dir"
ffmpeg_libs="$HOME/.local/share/ndstube/ffmpeg51/lib"
mkdir -p "$encoder_dir/x64"
for spec in avcodec:59 avdevice:59 avfilter:8 avformat:59 avutil:57 postproc:56 swresample:4 swscale:6; do
	name=${spec%%:*}
	version=${spec##*:}
	ln -sf "$ffmpeg_libs/lib${name}.so.${version}" "$encoder_dir/x64/lib${name}.so.${version}"
done
```

Start the relay with the published encoder and its FFmpeg runtime:

```sh
ffmpeg_libs="$HOME/.local/share/ndstube/ffmpeg51/lib"
encoder_dir="$HOME/.local/share/ndstube/FastVideoDSEncoder"
NDSTUBE_FASTVIDEO_ENCODER="env LD_LIBRARY_PATH=$ffmpeg_libs $encoder_dir/FastVideoDSEncoder -j 1" python /path/to/ndstube/service/server.py
```

Set `NDSTUBE_FASTVIDEO_CACHE` to change the cache directory. The first request for a video may take several minutes because the relay must download and encode it; later requests for the same video use the cache. The SD card must have enough free space for `testVideo.fv`. The client needs libfat/DLDI SD access, and the FastVideoDS Player must already be installed separately.

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