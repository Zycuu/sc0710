# sc0710 Capture Helper

Small user space capture helper for systems using the `sc0710` Linux driver for the Elgato 4K60 Pro Mk.2.

This helper does not replace the kernel driver. It wraps `ffmpeg`, `/dev/videoX`, and the ALSA PCM capture device created by the driver.

## CachyOS dependencies

```bash
sudo pacman -S --needed ffmpeg v4l-utils alsa-utils python
```

## Install locally

```bash
cd tools/sc0710-capture
./install.sh
```

## List detected devices

```bash
sc0710-capture list
```

Expected video device:

```text
/dev/video0
```

Expected audio device from the driver:

```text
hw:2,0 sc0710 HDMI
```

The ALSA card number can change. Use `sc0710-capture list` or `arecord -l` before recording.

## Record 1080p60 with audio

```bash
sc0710-capture record --video /dev/video0 --audio hw:2,0 --resolution 1920x1080 --fps 60 -o capture.mkv
```

## Record 4K60 with audio

```bash
sc0710-capture record --video /dev/video0 --audio hw:2,0 --resolution 3840x2160 --fps 60 -o capture-4k60.mkv
```

## Try raw read mode

The upstream Makefile reads `/dev/video0` as raw video. If V4L2 mode gives trouble, try:

```bash
sc0710-capture record --input-mode raw --video /dev/video0 --audio hw:2,0 --resolution 1920x1080 --fps 60 -o capture.mkv
```

## Preview

```bash
sc0710-capture preview --video /dev/video0 --audio hw:2,0 --resolution 1920x1080 --fps 60
```

## Driver status note

The upstream driver already has an ALSA capture path in `sc0710-audio.c`, but modern Arch style kernels need more work. The video side still depends on older `videobuf` calls in active paths, so this fork should modernize the video path to V4L2 videobuf2 and then harden the ALSA capture path.
