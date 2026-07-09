# V4L2 videobuf2 port plan

## Current status

The first compile focused videobuf2 port pass has been applied.

See `docs/videobuf2-port-status.md` for the active test instructions.

## Original failure

CachyOS 7.0.11 failed because `media/videobuf-vmalloc.h` was not present in the kernel headers.

That means the driver reached the real modern kernel compatibility problem. The old `videobuf` interface used by this source is not available in the CachyOS kernel headers.

## Why a small include fix was not enough

The missing include was only the first visible symptom. The driver still used legacy videobuf structures and functions in active runtime paths.

Examples in the original source:

- `struct sc0710_buffer` embedded `struct videobuf_buffer`.
- `struct sc0710_fh` contained `struct videobuf_queue vidq`.
- `sc0710_video_open()` called `videobuf_queue_vmalloc_init()`.
- V4L2 ioctl handlers called legacy `videobuf` queue helpers.
- `read()`, `poll()`, and `mmap()` called legacy `videobuf` file helpers.
- The DMA path completed buffers using old `VIDEOBUF` states.

So the correct fix was to port the capture path to videobuf2 rather than trying to recreate the removed legacy API.

## Design target

Keep the device visible as a normal V4L2 capture node and keep user space compatibility for tools like `v4l2-ctl`, ffmpeg, and OBS Studio.

## Applied direction

The first pass replaces the old videobuf data path with:

- `struct vb2_v4l2_buffer` in the driver video buffer wrapper
- `struct vb2_queue` owned by the video channel
- `vb2_vmalloc_memops`
- `vb2_ioctl_*` handlers
- `vb2_fop_read`, `vb2_fop_poll`, and `vb2_fop_mmap`
- `vb2_plane_vaddr()` for the DMA copy destination
- `vb2_set_plane_payload()` and `vb2_buffer_done()` for completed buffers

## Remaining work

1. Build on CachyOS and capture the next compiler output.
2. Fix any modern kernel API naming or struct field differences.
3. Confirm module load.
4. Confirm video device registration.
5. Confirm `v4l2-ctl --all`.
6. Confirm ffmpeg capture.
7. Only then test OBS.

## Important warning

This is the largest code change in the fork so far. It is currently a compile focused port and must be tested in small steps.
