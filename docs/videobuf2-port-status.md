# Initial videobuf2 port status

## What changed

The first compile focused videobuf2 port has been applied directly to the driver.

Touched files:

- `sc0710.h`
- `sc0710-video.c`
- `sc0710-dma-channel.c`

## Intent

The intent of this pass is to move the build past the old V4L video buffer API blocker.

It is not yet considered hardware safe or production ready.

## Main changes

`sc0710.h` now defines the driver video buffer around `struct vb2_v4l2_buffer` plus a driver owned list node.

`sc0710-video.c` now uses a `struct vb2_queue`, vb2 vmalloc memory operations, vb2 ioctl handlers, and vb2 file operations for normal V4L2 behavior.

`sc0710-dma-channel.c` now dequeues a driver owned `sc0710_buffer`, gets the user facing plane, copies the completed DMA chain into that plane, sets payload and timestamp, and completes the buffer through vb2.

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

The next compiler output will determine whether the remaining issues are API naming changes, missing vb2 helper declarations, struct field changes, or deeper driver logic problems.
