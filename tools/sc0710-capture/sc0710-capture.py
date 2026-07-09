#!/usr/bin/env python3
"""
sc0710 Capture Helper

Small ffmpeg wrapper for the Elgato 4K60 Pro Mk.2 when using the sc0710 Linux driver.
It records video from /dev/videoX and audio from the ALSA PCM device exported by the driver.
"""

from __future__ import annotations

import argparse
import datetime as _dt
import re
import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path


@dataclass
class AudioDevice:
    card: str
    device: str
    label: str

    @property
    def alsa_name(self) -> str:
        return f"hw:{self.card},{self.device}"


def run_capture(cmd: list[str]) -> tuple[int, str, str]:
    proc = subprocess.run(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    return proc.returncode, proc.stdout, proc.stderr


def need(binary: str) -> None:
    if shutil.which(binary) is None:
        raise SystemExit(f"Missing dependency: {binary}")


def list_video_devices() -> list[str]:
    devices: list[str] = []
    if shutil.which("v4l2-ctl"):
        _, out, err = run_capture(["v4l2-ctl", "--list-devices"])
        text = out + err
        for line in text.splitlines():
            stripped = line.strip()
            if stripped.startswith("/dev/video"):
                devices.append(stripped)
    if not devices:
        for path in sorted(Path("/dev").glob("video*")):
            devices.append(str(path))
    return devices


def list_audio_devices() -> list[AudioDevice]:
    devices: list[AudioDevice] = []
    if not shutil.which("arecord"):
        return devices
    _, out, err = run_capture(["arecord", "-l"])
    text = out + err
    pattern = re.compile(r"card\s+(\d+):\s+([^,]+),.*device\s+(\d+):\s+(.+)")
    for line in text.splitlines():
        match = pattern.search(line)
        if match:
            card, card_label, device, dev_label = match.groups()
            devices.append(AudioDevice(card=card, device=device, label=f"{card_label.strip()} {dev_label.strip()}"))
    return devices


def default_output() -> str:
    ts = _dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    return f"sc0710-capture-{ts}.mkv"


def build_ffmpeg_command(args: argparse.Namespace) -> list[str]:
    video = args.video
    audio = args.audio

    if not video:
        videos = list_video_devices()
        if not videos:
            raise SystemExit("No /dev/video device found. Load the sc0710 driver first.")
        video = videos[0]

    if not audio:
        audio_devs = list_audio_devices()
        sc0710_matches = [a for a in audio_devs if "sc0710" in a.label.lower() or "elgato" in a.label.lower()]
        audio = (sc0710_matches[0] if sc0710_matches else audio_devs[0]).alsa_name if audio_devs else None

    if not audio and not args.no_audio:
        raise SystemExit("No ALSA capture device found. Use --no-audio or pass --audio hw:CARD,DEV.")

    cmd = ["ffmpeg", "-hide_banner", "-y"]

    if args.input_mode == "raw":
        cmd += [
            "-thread_queue_size", str(args.video_queue),
            "-f", "rawvideo",
            "-pixel_format", args.pixel_format,
            "-video_size", args.resolution,
            "-framerate", str(args.fps),
            "-i", video,
        ]
    else:
        cmd += [
            "-thread_queue_size", str(args.video_queue),
            "-f", "v4l2",
            "-input_format", args.pixel_format,
            "-video_size", args.resolution,
            "-framerate", str(args.fps),
            "-i", video,
        ]

    if not args.no_audio:
        cmd += [
            "-thread_queue_size", str(args.audio_queue),
            "-f", "alsa",
            "-ac", str(args.audio_channels),
            "-ar", str(args.audio_rate),
            "-i", audio,
        ]

    if args.duration:
        cmd += ["-t", str(args.duration)]

    if args.preview:
        cmd += ["-f", "matroska", "-"]
        return cmd

    cmd += [
        "-c:v", args.video_codec,
        "-preset", args.preset,
        "-crf", str(args.crf),
        "-pix_fmt", args.output_pix_fmt,
    ]

    if not args.no_audio:
        cmd += ["-c:a", args.audio_codec, "-b:a", args.audio_bitrate]
    else:
        cmd += ["-an"]

    cmd += [args.output]
    return cmd


def cmd_list(_: argparse.Namespace) -> int:
    print("Video devices:")
    videos = list_video_devices()
    if videos:
        for dev in videos:
            print(f"  {dev}")
    else:
        print("  none detected")

    print("\nALSA capture devices:")
    audios = list_audio_devices()
    if audios:
        for dev in audios:
            marker = "  suggested" if "sc0710" in dev.label.lower() or "elgato" in dev.label.lower() else ""
            print(f"  {dev.alsa_name:10s} {dev.label}{marker}")
    else:
        print("  none detected")
    return 0


def cmd_record(args: argparse.Namespace) -> int:
    need("ffmpeg")
    cmd = build_ffmpeg_command(args)
    print("Running:")
    print(" ".join(subprocess.list2cmdline([part]) for part in cmd))
    return subprocess.call(cmd)


def cmd_preview(args: argparse.Namespace) -> int:
    need("ffmpeg")
    need("ffplay")
    args.preview = True
    ffmpeg_cmd = build_ffmpeg_command(args)
    ffplay_cmd = ["ffplay", "-hide_banner", "-fflags", "nobuffer", "-flags", "low_delay", "-"]
    print("Starting low latency preview. Press q in ffplay to quit.")
    ffmpeg = subprocess.Popen(ffmpeg_cmd, stdout=subprocess.PIPE)
    try:
        return subprocess.call(ffplay_cmd, stdin=ffmpeg.stdout)
    finally:
        ffmpeg.terminate()


def add_common_capture_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--video", default=None, help="Video device, such as /dev/video0")
    parser.add_argument("--audio", default=None, help="ALSA device, such as hw:2,0")
    parser.add_argument("--no-audio", action="store_true", help="Capture video only")
    parser.add_argument("--input-mode", choices=["v4l2", "raw"], default="v4l2", help="Use v4l2 or raw device reads")
    parser.add_argument("--resolution", default="1920x1080", help="Capture resolution")
    parser.add_argument("--fps", default="60", help="Capture frame rate")
    parser.add_argument("--pixel-format", default="yuyv422", help="Input pixel format")
    parser.add_argument("--audio-rate", type=int, default=48000, help="Audio sample rate")
    parser.add_argument("--audio-channels", type=int, default=2, help="Audio channel count")
    parser.add_argument("--video-queue", type=int, default=4096, help="ffmpeg video queue size")
    parser.add_argument("--audio-queue", type=int, default=1024, help="ffmpeg audio queue size")
    parser.add_argument("--duration", default=None, help="Optional duration, such as 00:05:00")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Capture helper for the sc0710 Elgato 4K60 Pro Mk.2 driver")
    sub = parser.add_subparsers(dest="command", required=True)

    p_list = sub.add_parser("list", help="List video and audio devices")
    p_list.set_defaults(func=cmd_list)

    p_record = sub.add_parser("record", help="Record to a Matroska file")
    add_common_capture_args(p_record)
    p_record.add_argument("--output", "-o", default=default_output(), help="Output file")
    p_record.add_argument("--video-codec", default="libx264", help="ffmpeg video codec")
    p_record.add_argument("--preset", default="veryfast", help="ffmpeg encoder preset")
    p_record.add_argument("--crf", type=int, default=18, help="x264 quality value")
    p_record.add_argument("--output-pix-fmt", default="yuv420p", help="Output pixel format")
    p_record.add_argument("--audio-codec", default="aac", help="ffmpeg audio codec")
    p_record.add_argument("--audio-bitrate", default="192k", help="Audio bitrate")
    p_record.set_defaults(func=cmd_record, preview=False)

    p_preview = sub.add_parser("preview", help="Open a low latency preview")
    add_common_capture_args(p_preview)
    p_preview.set_defaults(func=cmd_preview, preview=True, output="-")
    p_preview.add_argument("--video-codec", default="mpeg2video", help=argparse.SUPPRESS)
    p_preview.add_argument("--preset", default="ultrafast", help=argparse.SUPPRESS)
    p_preview.add_argument("--crf", type=int, default=23, help=argparse.SUPPRESS)
    p_preview.add_argument("--output-pix-fmt", default="yuv420p", help=argparse.SUPPRESS)
    p_preview.add_argument("--audio-codec", default="mp2", help=argparse.SUPPRESS)
    p_preview.add_argument("--audio-bitrate", default="192k", help=argparse.SUPPRESS)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
