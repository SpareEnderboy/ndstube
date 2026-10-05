#!/usr/bin/env python3
"""Small LAN search relay for the ndstube Nintendo DS client."""

from __future__ import annotations

import json
import math
import os
import re
import shutil
import subprocess
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit

VIDEO_WIDTH = 128
VIDEO_HEIGHT = 96
VIDEO_FPS = 6
AUDIO_RATE = 8000
MAX_VIDEO_SECONDS = 120
VIDEO_ID_PATTERN = re.compile(r"^[A-Za-z0-9_-]{11}$")


def parse_search_query(raw_query: str) -> str:
    query = raw_query.strip()
    if not query:
        raise ValueError("query must not be empty")
    if len(query) > 120:
        raise ValueError("query must be 120 characters or fewer")
    return query


def format_result(entry: dict) -> str:
    video_id = str(entry.get("id") or "")
    title = " ".join(str(entry.get("title") or "Untitled").split())
    duration = entry.get("duration")
    duration_text = str(int(duration)) if isinstance(duration, (int, float)) else ""
    return f"{video_id}\t{duration_text}\t{title[:100]}\n"


def search_videos(query: str) -> str:
    try:
        import yt_dlp
    except ImportError as error:
        raise RuntimeError("Install service requirements with: pip install -r service/requirements.txt") from error

    options = {"quiet": True, "no_warnings": True, "extract_flat": True, "skip_download": True}
    with yt_dlp.YoutubeDL(options) as downloader:
        result = downloader.extract_info(f"ytsearch8:{query}", download=False)

    entries = result.get("entries") or []
    return "".join(format_result(entry) for entry in entries if entry)


def parse_video_id(raw_video_id: str) -> str:
    if not VIDEO_ID_PATTERN.fullmatch(raw_video_id):
        raise ValueError("video id must be an 11-character YouTube id")
    return raw_video_id


def video_frame_count(duration: float) -> int:
    if duration <= 0:
        raise ValueError("video has no finite duration")
    return min(math.ceil(duration * VIDEO_FPS), MAX_VIDEO_SECONDS * VIDEO_FPS)


def resolve_video(video_id: str) -> tuple[str, str, float]:
    try:
        import yt_dlp
    except ImportError as error:
        raise RuntimeError("Install service requirements with: pip install -r service/requirements.txt") from error

    options = {
        "quiet": True,
        "no_warnings": True,
        "skip_download": True,
        "format": "bestvideo[height<=144]+bestaudio/bestvideo[height<=240]+bestaudio/bestvideo+bestaudio",
        "noplaylist": True,
    }
    with yt_dlp.YoutubeDL(options) as downloader:
        info = downloader.extract_info(f"https://www.youtube.com/watch?v={video_id}", download=False)

    formats = info.get("requested_formats") or []
    video_url = next((item.get("url") for item in formats if item.get("vcodec") != "none"), None)
    audio_url = next((item.get("url") for item in formats if item.get("acodec") != "none"), None)
    duration = info.get("duration")
    if not video_url or not audio_url or not isinstance(duration, (int, float)):
        raise ValueError("video or audio stream is unavailable, or video has no finite duration")
    return video_url, audio_url, duration


def start_frame_encoder(source_url: str, frame_count: int) -> subprocess.Popen:
    ffmpeg = shutil.which("ffmpeg")
    if ffmpeg is None:
        raise RuntimeError("FFmpeg is required; install it with: sudo pacman -S ffmpeg")

    video_filter = (
        f"fps={VIDEO_FPS},scale={VIDEO_WIDTH}:{VIDEO_HEIGHT}:force_original_aspect_ratio=decrease,"
        f"pad={VIDEO_WIDTH}:{VIDEO_HEIGHT}:(ow-iw)/2:(oh-ih)/2"
    )
    command = [
        ffmpeg, "-nostdin", "-loglevel", "error", "-i", source_url,
        "-vf", video_filter, "-frames:v", str(frame_count), "-an", "-sn", "-dn",
        "-pix_fmt", "rgb8", "-f", "rawvideo", "pipe:1",
    ]
    return subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, bufsize=0)


def start_audio_encoder(source_url: str, sample_count: int) -> subprocess.Popen:
    ffmpeg = shutil.which("ffmpeg")
    if ffmpeg is None:
        raise RuntimeError("FFmpeg is required; install it with: sudo pacman -S ffmpeg")

    command = [
        ffmpeg, "-nostdin", "-loglevel", "error", "-i", source_url,
        "-vn", "-sn", "-dn", "-ac", "1", "-ar", str(AUDIO_RATE),
        "-af", f"aresample={AUDIO_RATE},atrim=end_sample={sample_count},asetpts=N/SR/TB",
        "-acodec", "pcm_s8", "-f", "s8", "pipe:1",
    ]
    return subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, bufsize=0)


def audio_samples_for_frame(frame_index: int) -> int:
    return ((frame_index + 1) * AUDIO_RATE // VIDEO_FPS) - (frame_index * AUDIO_RATE // VIDEO_FPS)


def _read_exact(stream, count: int) -> bytes:
    chunks = []
    remaining = count
    while remaining:
        chunk = stream.read(remaining)
        if not chunk:
            break
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


class RelayHandler(BaseHTTPRequestHandler):
    server_version = "ndstube-relay/0.1"

    def do_GET(self) -> None:
        request = urlsplit(self.path)
        if request.path == "/health":
            self._send(200, "application/json; charset=utf-8", json.dumps({"ok": True}) + "\n")
            return
        if request.path == "/video":
            self._serve_video(parse_qs(request.query).get("id", [""])[0])
            return
        if request.path != "/search":
            self._send(404, "text/plain; charset=utf-8", "not found\n")
            return

        try:
            query = parse_search_query(parse_qs(request.query).get("q", [""])[0])
            results = search_videos(query)
        except ValueError as error:
            self._send(400, "text/plain; charset=utf-8", f"{error}\n")
            return
        except Exception as error:
            self._send(502, "text/plain; charset=utf-8", f"search failed: {error}\n")
            return

        self._send(200, "text/plain; charset=utf-8", results)

    def _serve_video(self, raw_video_id: str) -> None:
        encoder = None
        audio_encoder = None
        try:
            video_id = parse_video_id(raw_video_id)
            video_url, audio_url, duration = resolve_video(video_id)
            frame_count = video_frame_count(duration)
            encoder = start_frame_encoder(video_url, frame_count)
            audio_sample_count = frame_count * AUDIO_RATE // VIDEO_FPS
            audio_encoder = start_audio_encoder(audio_url, audio_sample_count)
        except ValueError as error:
            for process in (encoder, audio_encoder):
                if process is not None:
                    process.terminate()
                    process.wait()
            self._send(400, "text/plain; charset=utf-8", f"{error}\n")
            return
        except Exception as error:
            for process in (encoder, audio_encoder):
                if process is not None:
                    process.terminate()
                    process.wait()
            self._send(502, "text/plain; charset=utf-8", f"video unavailable: {error}\n")
            return

        frame_size = VIDEO_WIDTH * VIDEO_HEIGHT
        audio_sample_count = frame_count * AUDIO_RATE // VIDEO_FPS
        remaining = frame_count * frame_size + audio_sample_count
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(remaining))
        self.send_header("X-Frame-Width", str(VIDEO_WIDTH))
        self.send_header("X-Frame-Height", str(VIDEO_HEIGHT))
        self.send_header("X-Frame-Rate", str(VIDEO_FPS))
        self.send_header("X-Frame-Format", "rgb8")
        self.send_header("X-Audio-Rate", str(AUDIO_RATE))
        self.send_header("X-Audio-Format", "pcm_s8_mono")
        self.send_header("Connection", "close")
        self.end_headers()

        try:
            for frame_index in range(frame_count):
                frame = _read_exact(encoder.stdout, frame_size)
                audio = _read_exact(audio_encoder.stdout, audio_samples_for_frame(frame_index))
                if len(frame) != frame_size or len(audio) != audio_samples_for_frame(frame_index):
                    break
                self.wfile.write(frame)
                self.wfile.write(audio)
        except (BrokenPipeError, ConnectionResetError):
            pass
        finally:
            for process in (encoder, audio_encoder):
                process.stdout.close()
                if process.poll() is None:
                    process.terminate()
                process.wait()

    def _send(self, status: int, content_type: str, body: str) -> None:
        payload = body.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, format_string: str, *args: object) -> None:
        print(f"{self.address_string()} - {format_string % args}")


def main() -> None:
    host = os.environ.get("NDSTUBE_HOST", "0.0.0.0")
    port = int(os.environ.get("NDSTUBE_PORT", "8080"))
    server = ThreadingHTTPServer((host, port), RelayHandler)
    print(f"ndstube relay listening on http://{host}:{port}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping ndstube relay")
    finally:
        server.server_close()


if __name__ == "__main__":
    main()