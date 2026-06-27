#!/usr/bin/env python3
"""
sc0710-viewer

Small GStreamer launcher and workflow helper for the Elgato 4K60 Pro Mk.2
sc0710 Linux driver. It can build, load, unload, inspect, and run a simple
viewer with V4L2 video plus ALSA audio.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import signal
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Optional


DEFAULT_MODULE_ARGS = ["thread_dma_poll_interval_ms=2", "dma_status=0"]
MEDIA_SERVICES = ["wireplumber", "pipewire", "pipewire-pulse"]
DEPENDENCY_COMMANDS = ["python", "gst-launch-1.0", "v4l2-ctl", "arecord"]
ARCH_DEPENDENCY_PACKAGES = [
    "python",
    "gstreamer",
    "gst-plugins-base",
    "gst-plugins-good",
    "gst-plugins-bad",
    "v4l-utils",
    "alsa-utils",
]


@dataclass
class CaptureDevices:
    video_device: str
    audio_device: Optional[str]


@dataclass
class CommandResult:
    returncode: int
    output: str


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def run_text(command: Iterable[str], *, cwd: Optional[Path] = None, sudo: bool = False) -> str:
    cmd = list(command)
    if sudo:
        cmd.insert(0, "sudo")

    try:
        completed = subprocess.run(
            cmd,
            cwd=str(cwd) if cwd else None,
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        return completed.stdout
    except FileNotFoundError:
        return ""


def run_status(command: Iterable[str], *, cwd: Optional[Path] = None, sudo: bool = False) -> CommandResult:
    cmd = list(command)
    if sudo:
        cmd.insert(0, "sudo")

    try:
        completed = subprocess.run(
            cmd,
            cwd=str(cwd) if cwd else None,
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        return CommandResult(completed.returncode, completed.stdout)
    except FileNotFoundError as exc:
        return CommandResult(127, str(exc))


def run_live(command: Iterable[str], *, cwd: Optional[Path] = None, sudo: bool = False) -> int:
    cmd = list(command)
    if sudo:
        cmd.insert(0, "sudo")

    print("+ " + " ".join(cmd))
    completed = subprocess.run(cmd, cwd=str(cwd) if cwd else None, check=False)
    return completed.returncode


def command_exists(command: str) -> bool:
    return shutil.which(command) is not None


def missing_dependencies() -> list[str]:
    return [command for command in DEPENDENCY_COMMANDS if not command_exists(command)]


def install_dependencies() -> int:
    if shutil.which("pacman") is None:
        print("Automatic dependency install currently supports pacman based systems.", file=sys.stderr)
        print("Install GStreamer, v4l-utils, and alsa-utils with your distro package manager.", file=sys.stderr)
        return 2

    return run_live(["pacman", "-S", "--needed", *ARCH_DEPENDENCY_PACKAGES], sudo=True)


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


def module_loaded() -> bool:
    output = run_text(["lsmod"])
    return any(line.split()[0] == "sc0710" for line in output.splitlines() if line.strip())


def module_path_from_args(args: argparse.Namespace) -> Path:
    path = Path(args.module_path)
    if not path.is_absolute():
        path = repo_root() / path
    return path


def build_driver() -> int:
    root = repo_root()
    print(f"Building driver from {root}")
    clean_rc = run_live(["make", "clean-cachyos"], cwd=root)
    if clean_rc != 0:
        return clean_rc
    return run_live(["make", "cachyos"], cwd=root)


def load_driver(module_path: Path, module_args: list[str]) -> int:
    if module_loaded():
        print("sc0710 module is already loaded.")
        return 0

    if not module_path.exists():
        print(f"Module file does not exist: {module_path}", file=sys.stderr)
        print("Run with --build-driver first, or run --setup.", file=sys.stderr)
        return 2

    return run_live(["insmod", str(module_path), *module_args], sudo=True)


def unload_driver() -> int:
    if not module_loaded():
        print("sc0710 module is not loaded.")
        return 0

    result = run_status(["rmmod", "sc0710"], sudo=True)
    if result.output:
        print(result.output.rstrip())

    if result.returncode != 0:
        print("Unload failed. Something is probably holding /dev/video0 or the ALSA device open.")
        print("Try --safe-unload to stop PipeWire/WirePlumber, unload, then restart them.")
    return result.returncode


def stop_media_services() -> int:
    print("Stopping user media services that may probe capture devices...")
    return run_live(["systemctl", "--user", "stop", *MEDIA_SERVICES])


def start_media_services() -> int:
    print("Starting user media services...")
    return run_live(["systemctl", "--user", "start", *MEDIA_SERVICES])


def safe_unload_driver() -> int:
    stop_media_services()
    time.sleep(2)
    rc = unload_driver()
    start_media_services()
    return rc


def reload_driver(module_path: Path, module_args: list[str], *, safe: bool = False) -> int:
    rc = safe_unload_driver() if safe else unload_driver()
    if rc != 0:
        return rc
    return load_driver(module_path, module_args)


def print_detected_devices(devices: CaptureDevices) -> None:
    print(f"Video device: {devices.video_device}")
    print(f"Audio device: {devices.audio_device or 'not detected'}")
    print()
    print("v4l2-ctl --list-devices:")
    print(run_text(["v4l2-ctl", "--list-devices"]).rstrip() or "v4l2-ctl not available or no devices found")
    print()
    print("arecord -l:")
    print(run_text(["arecord", "-l"]).rstrip() or "arecord not available or no capture devices found")


def print_status(args: argparse.Namespace) -> None:
    devices = CaptureDevices(
        video_device=args.video_device or detect_video_device(),
        audio_device=args.audio_device or detect_audio_device(),
    )

    print("sc0710 status")
    print("=============")
    print(f"Repo root:      {repo_root()}")
    print(f"Module path:    {module_path_from_args(args)}")
    print(f"Module loaded:  {'yes' if module_loaded() else 'no'}")
    print(f"Video device:   {devices.video_device if Path(devices.video_device).exists() else devices.video_device + ' (missing)'}")
    print(f"Audio device:   {devices.audio_device or 'not detected'}")
    print()

    missing = missing_dependencies()
    if missing:
        print("Missing commands:")
        for command in missing:
            print(f"  - {command}")
        print()
    else:
        print("Required user-space commands: present")
        print()

    print("Loaded module entry:")
    print(run_text(["lsmod"]).split("\n")[0])
    for line in run_text(["lsmod"]).splitlines():
        if line.startswith("sc0710"):
            print(line)

    print()
    print_detected_devices(devices)


def print_dmesg(lines: int) -> int:
    result = run_status(["sh", "-c", f"dmesg | tail -n {lines}"], sudo=True)
    if result.output:
        print(result.output.rstrip())
    return result.returncode


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


def ensure_runtime_ready(args: argparse.Namespace) -> int:
    missing = missing_dependencies()
    if missing:
        print("Missing required commands:", ", ".join(missing), file=sys.stderr)
        print("Run with --install-deps, or install the packages shown in the README.", file=sys.stderr)
        return 2

    if not module_loaded() and not args.no_auto_load:
        print("sc0710 module is not loaded. Loading it now...")
        rc = load_driver(module_path_from_args(args), args.module_arg)
        if rc != 0:
            return rc

    return 0


def run_viewer(args: argparse.Namespace) -> int:
    rc = ensure_runtime_ready(args)
    if rc != 0:
        return rc

    devices = CaptureDevices(
        video_device=args.video_device or detect_video_device(),
        audio_device=args.audio_device or detect_audio_device(),
    )

    if not os.path.exists(devices.video_device):
        print(f"Video device does not exist: {devices.video_device}", file=sys.stderr)
        print("Load the sc0710 module first, or run with --setup.", file=sys.stderr)
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


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Manage and display sc0710 V4L2 video plus sc0710 ALSA audio with GStreamer."
    )

    workflow = parser.add_argument_group("workflow actions")
    workflow.add_argument("--setup", action="store_true", help="Install dependencies, build the driver, and load it, then exit")
    workflow.add_argument("--install-deps", action="store_true", help="Install viewer dependencies with pacman")
    workflow.add_argument("--build-driver", action="store_true", help="Run make clean-cachyos and make cachyos")
    workflow.add_argument("--load-driver", action="store_true", help="Load sc0710.ko with sudo insmod")
    workflow.add_argument("--unload-driver", action="store_true", help="Unload sc0710 with sudo rmmod")
    workflow.add_argument("--safe-unload", action="store_true", help="Stop PipeWire/WirePlumber, unload sc0710, then restart services")
    workflow.add_argument("--reload-driver", action="store_true", help="Unload and load sc0710 again")
    workflow.add_argument("--safe-reload", action="store_true", help="Safe unload, then load sc0710 again")
    workflow.add_argument("--status", action="store_true", help="Print module, video, audio, and dependency status")
    workflow.add_argument("--list", action="store_true", help="List detected video and audio devices")
    workflow.add_argument("--dmesg", action="store_true", help="Show recent kernel log lines with sudo dmesg")
    workflow.add_argument("--stop-media-services", action="store_true", help="Stop PipeWire/WirePlumber user services")
    workflow.add_argument("--start-media-services", action="store_true", help="Start PipeWire/WirePlumber user services")

    driver = parser.add_argument_group("driver options")
    driver.add_argument("--module-path", default="sc0710.ko", help="Path to sc0710.ko, default: repo root sc0710.ko")
    driver.add_argument("--module-arg", action="append", default=DEFAULT_MODULE_ARGS.copy(), help="Module argument for insmod. Can be repeated")
    driver.add_argument("--no-auto-load", action="store_true", help="Do not automatically load the module before launching viewer")

    viewer = parser.add_argument_group("viewer options")
    viewer.add_argument("--video-device", help="V4L2 video node, for example /dev/video0")
    viewer.add_argument("--audio-device", help="ALSA capture device, for example hw:0,0")
    viewer.add_argument("--no-audio", action="store_true", help="Start video only")
    viewer.add_argument("--width", type=int, help="Requested capture width")
    viewer.add_argument("--height", type=int, help="Requested capture height")
    viewer.add_argument("--framerate", type=int, help="Requested capture framerate")
    viewer.add_argument("--format", default="YUY2", help="GStreamer video format cap, default: YUY2")
    viewer.add_argument("--dry-run", action="store_true", help="Print the GStreamer command without running it")
    viewer.add_argument("--verbose", action="store_true", help="Pass -v to gst-launch-1.0")
    viewer.add_argument("--dmesg-lines", type=int, default=160, help="Number of dmesg lines to show")

    return parser.parse_args()


def main() -> int:
    args = parse_args()
    module_path = module_path_from_args(args)

    if args.install_deps or args.setup:
        rc = install_dependencies()
        if rc != 0:
            return rc

    if args.build_driver or args.setup:
        rc = build_driver()
        if rc != 0:
            return rc

    if args.stop_media_services:
        return stop_media_services()

    if args.start_media_services:
        return start_media_services()

    if args.safe_unload:
        return safe_unload_driver()

    if args.unload_driver:
        return unload_driver()

    if args.reload_driver or args.safe_reload:
        return reload_driver(module_path, args.module_arg, safe=args.safe_reload)

    if args.load_driver or args.setup:
        rc = load_driver(module_path, args.module_arg)
        if rc != 0:
            return rc
        if args.setup:
            print("Setup complete. Run without --setup to start the viewer.")
            return 0

    if args.status:
        print_status(args)
        return 0

    if args.list:
        devices = CaptureDevices(
            video_device=args.video_device or detect_video_device(),
            audio_device=args.audio_device or detect_audio_device(),
        )
        print_detected_devices(devices)
        return 0

    if args.dmesg:
        return print_dmesg(args.dmesg_lines)

    return run_viewer(args)


if __name__ == "__main__":
    raise SystemExit(main())
