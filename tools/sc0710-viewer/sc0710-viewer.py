#!/usr/bin/env python3
"""
sc0710-viewer

Small GStreamer launcher for the Elgato 4K60 Pro Mk.2 sc0710 Linux driver.
It displays V4L2 video and plays ALSA capture audio without requiring OBS.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import signal
import subprocess
import sys
from dataclasses import dataclass
from typing import Iterable, Optional


@dataclass
class CaptureDevices:
    video_device: str
    audio_device: Optional[str]


def run_text(command: Iterable[str]) -> str:
    try:
        completed = subprocess.run(
            list(command),
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        return completed.stdout
    except FileNotFoundError:
        return ""


def detect_video_device() -> str:
    """Find the first V4L2 node in a sc0710 or Elgato device block."""
    output = run_text(["v4l2-ctl", "--list-devices"])
    current_matches = False

    for raw_line in output.splitlines():
        line = raw_line.rstrip()
        lower = line.lower()

        if line and not line.startswith((" ", "\t")):
            current_matches = "sc0710" in lower or "elgato" in lower or "4k60" in lower
            continue

        if current_matches:
            match = re.search(r"(/dev/video\d+)", line)
            if match:
                return match.group(1)

    return "/dev/video0"


def detect_audio_device() -> Optional[str]:
    """Find the first ALSA capture device whose card or device name mentions sc0710."""
    output = run_text(["arecord", "-l"])
    pattern = re.compile(r"card\s+(\d+):.*sc0710.*device\s+(\d+):", re.IGNORECASE)

    for line in output.splitlines():
        match = pattern.search(line)
        if match:
            return f"hw:{match.group(1)},{match.group(2)}"

    return None


def build_pipeline(args: argparse.Namespace, devices: CaptureDevices) -> list[str]:
    command: list[str] = ["gst-launch-1.0", "-e"]

    if args.verbose:
        command.append("-v")

    command.extend([
        "v4l2src",
        f"device={devices.video_device}",
        "!",
    ])

    caps_parts = ["video/x-raw"]
    if args.format:
        caps_parts.append(f"format={args.format}")
    if args.width and args.height:
        caps_parts.append(f"width={args.width}")
        caps_parts.append(f"height={args.height}")
    if args.framerate:
        caps_parts.append(f"framerate={args.framerate}/1")

    if len(caps_parts) > 1:
        command.extend([",".join(caps_parts), "!"])

    command.extend([
        "queue",
        "!",
        "videoconvert",
        "!",
        "autovideosink",
        "sync=false",
    ])

    if not args.no_audio and devices.audio_device:
        command.extend([
            "alsasrc",
            f"device={devices.audio_device}",
            "!",
            "queue",
            "!",
            "audioconvert",
            "!",
            "audioresample",
            "!",
            "autoaudiosink",
            "sync=false",
        ])

    return command


def print_detected_devices(devices: CaptureDevices) -> None:
    print(f"Video device: {devices.video_device}")
    print(f"Audio device: {devices.audio_device or 'not detected'}")
    print()
    print("v4l2-ctl --list-devices:")
    print(run_text(["v4l2-ctl", "--list-devices"]).rstrip() or "v4l2-ctl not available or no devices found")
    print()
    print("arecord -l:")
    print(run_text(["arecord", "-l"]).rstrip() or "arecord not available or no capture devices found")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Display sc0710 V4L2 video and play sc0710 ALSA audio with GStreamer."
    )
    parser.add_argument("--video-device", help="V4L2 video node, for example /dev/video0")
    parser.add_argument("--audio-device", help="ALSA capture device, for example hw:0,0")
    parser.add_argument("--no-audio", action="store_true", help="Start video only")
    parser.add_argument("--width", type=int, help="Requested capture width")
    parser.add_argument("--height", type=int, help="Requested capture height")
    parser.add_argument("--framerate", type=int, help="Requested capture framerate")
    parser.add_argument("--format", default="YUY2", help="GStreamer video format cap, default: YUY2")
    parser.add_argument("--list", action="store_true", help="List detected video and audio devices")
    parser.add_argument("--dry-run", action="store_true", help="Print the GStreamer command without running it")
    parser.add_argument("--verbose", action="store_true", help="Pass -v to gst-launch-1.0")
    args = parser.parse_args()

    devices = CaptureDevices(
        video_device=args.video_device or detect_video_device(),
        audio_device=args.audio_device or detect_audio_device(),
    )

    if args.list:
        print_detected_devices(devices)
        return 0

    if not os.path.exists(devices.video_device):
        print(f"Video device does not exist: {devices.video_device}", file=sys.stderr)
        print("Load the sc0710 module first, then try again.", file=sys.stderr)
        return 2

    if shutil.which("gst-launch-1.0") is None:
        print("gst-launch-1.0 was not found. Install GStreamer tools/plugins first.", file=sys.stderr)
        return 2

    if not args.no_audio and not devices.audio_device:
        print("Warning: sc0710 ALSA capture device was not detected. Starting video only.", file=sys.stderr)

    command = build_pipeline(args, devices)

    if args.dry_run:
        print(" ".join(command))
        return 0

    print("Starting sc0710 viewer. Press Ctrl+C to stop.")
    print(f"Video: {devices.video_device}")
    print(f"Audio: {devices.audio_device or 'disabled/not detected'}")

    process = subprocess.Popen(command)

    def stop_process(signum: int, frame: object) -> None:
        if process.poll() is None:
            process.terminate()

    signal.signal(signal.SIGINT, stop_process)
    signal.signal(signal.SIGTERM, stop_process)

    try:
        return process.wait()
    except KeyboardInterrupt:
        stop_process(signal.SIGINT, None)
        return process.wait()


if __name__ == "__main__":
    raise SystemExit(main())
