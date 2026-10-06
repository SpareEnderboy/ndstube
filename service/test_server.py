import io
import sys
import tempfile
import unittest
import threading
from pathlib import Path
from http.server import ThreadingHTTPServer
from unittest.mock import patch
from urllib.error import HTTPError
from urllib.request import urlopen

from server import (
    VIDEO_FPS,
    VIDEO_HEIGHT,
    VIDEO_WIDTH,
    AUDIO_RATE,
    RelayHandler,
    _download_fastvideo_source,
    audio_samples_for_frame,
    ensure_fastvideo_file,
    format_result,
    parse_search_query,
    parse_video_id,
    video_frame_count,
)


class FakeEncoder:
    def __init__(self, frames):
        self.stdout = io.BytesIO(frames)

    def poll(self):
        return 0

    def wait(self):
        return 0

    def terminate(self):
        return None


class SearchFormattingTests(unittest.TestCase):
    def test_query_is_trimmed(self):
        self.assertEqual(parse_search_query("  dsi homebrew  "), "dsi homebrew")

    def test_empty_and_long_queries_are_rejected(self):
        with self.assertRaises(ValueError):
            parse_search_query("  ")
        with self.assertRaises(ValueError):
            parse_search_query("x" * 121)

    def test_result_fields_are_single_line_and_tab_safe(self):
        row = format_result({"id": "abc123", "title": "A\ttitle\nwith lines", "duration": 42.9})
        self.assertEqual(row, "abc123\t42\tA title with lines\n")

    def test_video_ids_and_frame_counts_are_bounded(self):
        self.assertEqual(parse_video_id("abcdefghijk"), "abcdefghijk")
        with self.assertRaises(ValueError):
            parse_video_id("invalid")
        self.assertEqual(video_frame_count(1), VIDEO_FPS)
        self.assertEqual(video_frame_count(1000), 120 * VIDEO_FPS)
        self.assertEqual(sum(audio_samples_for_frame(index) for index in range(VIDEO_FPS)), AUDIO_RATE)
        with self.assertRaises(ValueError):
            video_frame_count(0)

    def test_search_route_returns_formatted_results(self):
        server = ThreadingHTTPServer(("127.0.0.1", 0), RelayHandler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            with patch("server.search_videos", return_value="abc123\t42\tA title\n") as search:
                with urlopen(f"http://127.0.0.1:{server.server_port}/search?q=hello") as response:
                    self.assertEqual(response.status, 200)
                    self.assertEqual(response.read().decode(), "abc123\t42\tA title\n")
                search.assert_called_once_with("hello")
        finally:
            server.shutdown()
            server.server_close()
            thread.join()

    def test_search_route_rejects_empty_query(self):
        server = ThreadingHTTPServer(("127.0.0.1", 0), RelayHandler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            with self.assertRaises(HTTPError) as error:
                urlopen(f"http://127.0.0.1:{server.server_port}/search")
            self.assertEqual(error.exception.code, 400)
            error.exception.close()
        finally:
            server.shutdown()
            server.server_close()
            thread.join()


class FastVideoTests(unittest.TestCase):
    def test_encoded_video_is_cached_and_reused(self):
        encoded = b"FVDS" + bytes(0x1C - 4)
        with tempfile.TemporaryDirectory() as cache_directory:
            cache_path = Path(cache_directory)

            def download_source(video_id, directory):
                source = directory / "source.mkv"
                source.write_bytes(b"source")
                return source

            def encode(command, **kwargs):
                Path(command[-1]).write_bytes(encoded)
                return None

            with patch("server.FASTVIDEO_CACHE_DIR", cache_path), patch(
                "server.FASTVIDEO_ENCODER", "/opt/FastVideoDSEncoder"
            ), patch("server._download_fastvideo_source", side_effect=download_source) as download, patch(
                "server.subprocess.run", side_effect=encode
            ) as run_encoder:
                first = ensure_fastvideo_file("abcdefghijk")
                second = ensure_fastvideo_file("abcdefghijk")

            self.assertEqual(first, second)
            self.assertEqual(first.read_bytes(), encoded)
            download.assert_called_once()
            run_encoder.assert_called_once()

    def test_fastvideo_route_streams_cached_file(self):
        payload = b"FVDS" + bytes(40)
        with tempfile.TemporaryDirectory() as directory:
            encoded_file = Path(directory) / "abcdefghijk.fv"
            encoded_file.write_bytes(payload)
            server = ThreadingHTTPServer(("127.0.0.1", 0), RelayHandler)
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            try:
                with patch("server.ensure_fastvideo_file", return_value=encoded_file):
                    with urlopen(f"http://127.0.0.1:{server.server_port}/fastvideo?id=abcdefghijk") as response:
                        self.assertEqual(response.status, 200)
                        self.assertEqual(response.headers["X-Video-Format"], "FastVideoDS")
                        self.assertEqual(response.headers["Content-Length"], str(len(payload)))
                        self.assertEqual(response.read(), payload)
            finally:
                server.shutdown()
                server.server_close()
                thread.join()

    def test_source_download_is_limited_to_relay_max_duration(self):
        captured = {}

        class FakeYoutubeDL:
            def __init__(self, options):
                captured.update(options)

            def __enter__(self):
                return self

            def __exit__(self, *_args):
                return False

            def download(self, urls):
                captured["urls"] = urls
                Path(captured["outtmpl"].replace("%(ext)s", "mkv")).write_bytes(b"source")

        fake_module = type("FakeYtDlp", (), {"YoutubeDL": FakeYoutubeDL})
        with tempfile.TemporaryDirectory() as directory, patch.dict(sys.modules, {"yt_dlp": fake_module}):
            source_path = _download_fastvideo_source("abcdefghijk", Path(directory))

        self.assertEqual(source_path.suffix, ".mkv")
        self.assertEqual(captured["download_ranges"]({}, None), [{
            "start_time": 0,
            "end_time": 120,
        }])
        self.assertTrue(captured["force_keyframes_at_cuts"])

    def test_video_route_streams_sized_rgb8_frames(self):
        frame_count = VIDEO_FPS
        frame = bytes([0xA5]) * (VIDEO_WIDTH * VIDEO_HEIGHT)
        audio = bytes([0x5A]) * AUDIO_RATE
        expected_body = b"".join(
            frame + audio[
                sum(audio_samples_for_frame(previous) for previous in range(index)):
                sum(audio_samples_for_frame(previous) for previous in range(index + 1))
            ]
            for index in range(frame_count)
        )
        server = ThreadingHTTPServer(("127.0.0.1", 0), RelayHandler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            with patch("server.resolve_video", return_value=("https://example.invalid/video", "https://example.invalid/audio", 1.0)):
                with patch("server.start_frame_encoder", return_value=FakeEncoder(frame * frame_count)) as start_video:
                    with patch("server.start_audio_encoder", return_value=FakeEncoder(audio)) as start_audio:
                        with urlopen(f"http://127.0.0.1:{server.server_port}/video?id=abcdefghijk") as response:
                            self.assertEqual(response.status, 200)
                            self.assertEqual(response.headers["X-Frame-Format"], "rgb8")
                            self.assertEqual(response.headers["X-Frame-Width"], str(VIDEO_WIDTH))
                            self.assertEqual(response.headers["X-Frame-Height"], str(VIDEO_HEIGHT))
                            self.assertEqual(response.headers["X-Frame-Rate"], str(VIDEO_FPS))
                            self.assertEqual(response.headers["X-Audio-Format"], "pcm_s8_mono")
                            self.assertEqual(response.headers["X-Audio-Rate"], str(AUDIO_RATE))
                            self.assertEqual(response.read(), expected_body)
                        start_video.assert_called_once_with("https://example.invalid/video", frame_count)
                        start_audio.assert_called_once_with("https://example.invalid/audio", AUDIO_RATE)
        finally:
            server.shutdown()
            server.server_close()
            thread.join()

    def test_video_route_rejects_invalid_id(self):
        server = ThreadingHTTPServer(("127.0.0.1", 0), RelayHandler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            with self.assertRaises(HTTPError) as error:
                urlopen(f"http://127.0.0.1:{server.server_port}/video?id=bad")
            self.assertEqual(error.exception.code, 400)
            error.exception.close()
        finally:
            server.shutdown()
            server.server_close()
            thread.join()


if __name__ == "__main__":
    unittest.main()