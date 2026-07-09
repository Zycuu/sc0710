# Initial videobuf2 port status

## Current status

The module now builds on CachyOS with the `make cachyos` target.

Observed successful build result:

```text
LD [M]  sc0710.o
MODPOST Module.symvers
CC [M]  sc0710.mod.o
CC [M]  .module-common.o
LD [M]  sc0710.ko
BTF [M] sc0710.ko
```

## What changed

The first compile focused videobuf2 port has been applied directly to the driver.

Touched files:

- `sc0710.h`
- `sc0710-video.c`
- `sc0710-dma-channel.c`

## Intent

The intent of this pass was to move the build past the old V4L video buffer API blocker.

It is still not considered hardware safe or production ready until load and capture tests pass.

## Main changes

`sc0710.h` now defines the driver video buffer around `struct vb2_v4l2_buffer` plus a driver owned list node.

`sc0710-video.c` now uses a `struct vb2_queue`, vb2 vmalloc memory operations, vb2 ioctl handlers, and vb2 file operations for normal V4L2 behavior.

`sc0710-dma-channel.c` now dequeues a driver owned `sc0710_buffer`, gets the user facing plane, copies the completed DMA chain into that plane, sets payload and timestamp, and completes the buffer through vb2.

## Next test phase

The next phase is cautious module load testing.

Do not test capture yet. First confirm:

- module loads without a kernel fault
- module unloads cleanly
- expected device nodes appear
- dmesg does not report DMA, V4L2, ALSA, or PCI errors during load and unload

## Load test commands

From the repo root on CachyOS:

```bash
sudo dmesg -C
sudo insmod ./sc0710.ko thread_dma_poll_interval_ms=2 dma_status=0
sleep 3
dmesg | tail -n 120
lsmod | grep sc0710
v4l2-ctl --list-devices
arecord -l
sudo rmmod sc0710
dmesg | tail -n 120
```
