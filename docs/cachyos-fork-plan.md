# CachyOS fork plan

This fork targets the Elgato 4K60 Pro Mk.2 using the existing `sc0710` reverse engineered Linux driver as the base.

## Current upstream state

The upstream project already contains:

- `sc0710-video.c` for the V4L2 video device
- `sc0710-audio.c` for an ALSA PCM capture device named `sc0710 HDMI`
- `sc0710-dma-channel.c` for the shared video and audio DMA queue handling

The README states that audio support was added as PCM 16 bit, 48 kHz, and that video plus audio capture can work through ffmpeg on older supported systems.

## Main CachyOS problem

CachyOS normally tracks modern Arch style kernels. The current driver still uses the older V4L `videobuf` API in active capture paths. The source also contains a partial `vb2_queue` setup, but the live open, read, poll, mmap, and buffer queue paths still go through legacy `videobuf` calls.

That means the real fork work is not just packaging. The video path needs a proper migration to videobuf2.

## Audio status

The audio path is real, but fragile:

- Audio samples are delivered only when an ALSA capture substream is open.
- The DMA thread can deliver audio while no capture app is reading, which causes noisy kernel logs.
- The PCM buffer is manually allocated in `hw_params` instead of using ALSA helper allocation.
- `snd_pcm_period_elapsed()` is called for each DMA audio chunk rather than when an ALSA period boundary is crossed.

## Milestones

1. Confirm build failures on the current CachyOS kernel.
2. Remove or isolate legacy `videobuf` dependencies.
3. Migrate active V4L2 capture to videobuf2 callbacks.
4. Keep `/dev/videoX` compatibility for OBS and ffmpeg.
5. Harden the ALSA capture path.
6. Add Arch and CachyOS friendly DKMS packaging.
7. Keep a small user space helper for easy testing and capture.

## Debug commands

```bash
lspci -nn | grep -i -E 'elgato|12ab|0710'
dmesg -w
v4l2-ctl --list-devices
v4l2-ctl --device=/dev/video0 --all
arecord -l
arecord --dump-hw-params -D hw:2,0
cat /proc/sc0710-state
```

## Baseline capture command

```bash
ffmpeg -hide_banner -y \
  -thread_queue_size 4096 -f v4l2 -input_format yuyv422 -video_size 1920x1080 -framerate 60 -i /dev/video0 \
  -thread_queue_size 1024 -f alsa -ac 2 -ar 48000 -i hw:2,0 \
  -c:v libx264 -preset veryfast -crf 18 -pix_fmt yuv420p \
  -c:a aac -b:a 192k capture.mkv
```

## Working rule

Do not touch `master` directly. Use feature branches and pull requests for reviewable changes.
