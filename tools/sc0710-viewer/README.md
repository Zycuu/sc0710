# sc0710 viewer

`sc0710-viewer.py` is a small GStreamer based viewer and workflow helper for the sc0710 driver.

It does not require OBS.

It is meant to:

- build the driver on CachyOS
- load and unload `sc0710.ko`
- display the Elgato V4L2 video node, usually `/dev/video0`
- play the sc0710 HDMI ALSA capture device, usually `hw:0,0`
- show status and device detection output without needing several copy paste blocks

## Pull the latest app

From the repo root:

```bash
git pull
```

## One command setup

This installs the user space dependencies, builds the driver, and loads the module:

```bash
python tools/sc0710-viewer/sc0710-viewer.py --setup
```

The setup action runs these kinds of actions for you:

- `sudo pacman -S --needed ...`
- `make clean-cachyos`
- `make cachyos`
- `sudo insmod ./sc0710.ko thread_dma_poll_interval_ms=2 dma_status=0`

If the module is already loaded, the app will say so and keep going.

## Normal use

Most of the time, after setup, use:

```bash
python tools/sc0710-viewer/sc0710-viewer.py
```

The app will automatically load the driver first if it is not already loaded.

## Video only

```bash
python tools/sc0710-viewer/sc0710-viewer.py --no-audio
```

## Request a capture mode

```bash
python tools/sc0710-viewer/sc0710-viewer.py --width 1920 --height 1080 --framerate 60
```

## Status and device listing

Show module status, dependency status, detected V4L2 devices, and detected ALSA capture devices:

```bash
python tools/sc0710-viewer/sc0710-viewer.py --status
```

Only list detected devices:

```bash
python tools/sc0710-viewer/sc0710-viewer.py --list
```

Show recent kernel logs:

```bash
python tools/sc0710-viewer/sc0710-viewer.py --dmesg
```

## Driver actions

Build the driver:

```bash
python tools/sc0710-viewer/sc0710-viewer.py --build-driver
```

Load the driver:

```bash
python tools/sc0710-viewer/sc0710-viewer.py --load-driver
```

Unload the driver:

```bash
python tools/sc0710-viewer/sc0710-viewer.py --unload-driver
```

If unload fails because PipeWire or WirePlumber is probing `/dev/video0`, use safe unload:

```bash
python tools/sc0710-viewer/sc0710-viewer.py --safe-unload
```

Safe unload stops `wireplumber`, `pipewire`, and `pipewire-pulse`, unloads `sc0710`, then restarts those services.

Reload the driver:

```bash
python tools/sc0710-viewer/sc0710-viewer.py --safe-reload
```

## Manual device selection

If automatic detection chooses the wrong devices:

```bash
python tools/sc0710-viewer/sc0710-viewer.py --video-device /dev/video0 --audio-device hw:0,0
```

## Print the GStreamer command without running it

```bash
python tools/sc0710-viewer/sc0710-viewer.py --dry-run
```

## Stop the viewer

Press `Ctrl+C` in the terminal that launched it.

## Current limitations

This is still a first pass validation app. It intentionally keeps the media pipeline simple.

It does not yet include:

- a custom GUI
- recording
- latency controls
- resolution dropdowns
- audio device dropdowns
- a persistent installed desktop launcher

Those can be added after the driver proves stable during basic capture tests.
