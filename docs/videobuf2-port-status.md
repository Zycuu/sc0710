# Initial videobuf2 port status

## What changed

The first compile focused videobuf2 port has been applied directly to the driver.

Touched files:

- `sc0710.h`
- `sc0710-video.c`
- `sc0710-dma-channel.c`

## Intent

The intent of this pass is to move the build past the missing legacy header:

```text
media/videobuf-vmalloc.h
```

It is not yet considered hardware safe or production ready.

## Main changes

`sc0710.h` now removes the direct legacy `videobuf-vmalloc` include and defines the driver video buffer around `struct vb2_v4l2_buffer` plus a driver owned list node.

`sc0710-video.c` now uses a `struct vb2_queue`, `vb2_vmalloc_memops`, `vb2_ioctl_*` handlers, and `vb2_fop_read`, `vb2_fop_poll`, and `vb2_fop_mmap` for normal V4L2 file operations.

`sc0710-dma-channel.c` now dequeues a driver owned `sc0710_buffer`, gets the user facing plane with `vb2_plane_vaddr()`, copies the completed DMA chain into that plane, sets payload and timestamp, and completes the buffer with `vb2_buffer_done()`.

## Expected next test

From the repo root on CachyOS:

```bash
git pull
git switch cachyos-driver-foundation
make clean-cachyos
make cachyos
```

## Expected result

This may still fail. That is acceptable.

The important question is whether the build moves past:

```text
fatal error: 'media/videobuf-vmalloc.h' file not found
```

The next compiler output will determine whether the remaining issues are API naming changes, missing vb2 helper declarations, struct field changes, or deeper driver logic problems.
