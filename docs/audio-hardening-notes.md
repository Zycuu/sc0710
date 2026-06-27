# Audio hardening notes

## Why audio was patched before video testing

The CachyOS build is currently blocked by the old V4L `videobuf` video path, but the ALSA audio path can still be made safer before the larger videobuf2 port starts.

The audio changes are intentionally smaller than the video work and stay inside `sc0710-audio.c`.

## Problems addressed

The previous audio delivery function returned noisy errors when the DMA thread delivered audio while no ALSA capture client was active. That could happen when the DMA channel is running but no user space app has opened and started the PCM stream.

The previous code also called `snd_pcm_period_elapsed()` every delivered DMA chunk instead of tracking when the configured ALSA period boundary was crossed.

The previous PCM buffer allocation used `kzalloc()`, but the page callback used `vmalloc_to_page()`. The applied change now uses `vzalloc()` and `vfree()` so the allocation style matches the page callback.

## Current behavior

The audio path now:

- Tracks active capture state internally.
- Ignores samples if ALSA capture is not actively running.
- Guards missing chip, substream, runtime, DMA area, and zero buffer size without log spam.
- Resets the audio buffer pointer and period pointer on open, prepare, close, hardware free, and register.
- Sets capture active on START, RESUME, and PAUSE_RELEASE.
- Sets capture inactive on STOP, SUSPEND, and PAUSE_PUSH.
- Calls `snd_pcm_period_elapsed()` only after the ALSA period size has been crossed.
- Uses a vmalloc backed runtime buffer with a guarded `.page` callback.

## Test status

Not hardware tested yet.

The driver still cannot compile on CachyOS until the legacy video `videobuf` path is ported to videobuf2.
