# sc0710 viewer

`sc0710-viewer.py` is a small GStreamer based viewer app for the sc0710 driver.

It does not require OBS.

It is meant to:

- display the Elgato V4L2 video node, usually `/dev/video0`
- play the sc0710 HDMI ALSA capture device, usually `hw:0,0`
- provide a simple test app while the driver is being validated

## Dependencies

On CachyOS or another Arch based distribution, install the needed user space tools and GStreamer plugins if they are missing:

```bash
sudo pacman -S --needed python gstreamer gst-plugins-base gst-plugins-good gst-plugins-bad v4l-utils alsa-utils
```

## Load the driver first

From the repo root:

```bash
sudo insmod ./sc0710.ko thread_dma_poll_interval_ms=2 dma_status=0
```

If the module is already loaded, `insmod` may print `File exists`. That means the module is already present.

## List detected devices

```bash
python tools/sc0710-viewer/sc0710-viewer.py --list
```

Expected devices look like this:

```text
Video device: /dev/video0
Audio device: hw:0,0
```

## Start the viewer

Start with automatic device detection:

```bash
python tools/sc0710-viewer/sc0710-viewer.py
```

If you want to request a specific mode:

```bash
python tools/sc0710-viewer/sc0710-viewer.py --width 1920 --height 1080 --framerate 60
```

If you want video only:

```bash
python tools/sc0710-viewer/sc0710-viewer.py --no-audio
```

If detection chooses the wrong devices, specify them directly:

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

This is a first pass validation app. It intentionally keeps the pipeline simple.

It does not yet include:

- a custom GUI
- recording
- latency controls
- resolution dropdowns
- audio device dropdowns
- automatic driver load and unload

Those can be added after the driver proves stable during basic capture tests.
