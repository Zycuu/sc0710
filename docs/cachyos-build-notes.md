# CachyOS build notes

## Test system

CachyOS kernel build tree observed at `/lib/modules/7.0.11-1-cachyos/build`.

## First observed failure

The first build failed because the running kernel was built with Clang, while the external module build used GCC. GCC did not understand several LLVM style kernel build flags.

## Fix added in this fork branch

The Makefile now supports these targets:

```bash
make cachyos
make clean-cachyos
```

Those targets pass `LLVM=1` into kbuild.

## Second observed failure

After rebuilding with `LLVM=1`, the build reached the source compatibility issue where the CachyOS kernel headers did not provide the old `videobuf-vmalloc` header used by the driver.

## Meaning

This confirmed the driver source depends on an old V4L buffer API that needs to be ported to videobuf2.

## First videobuf2 port pass

A first compile focused videobuf2 port pass is now applied in:

- `sc0710.h`
- `sc0710-video.c`
- `sc0710-dma-channel.c`

The next build should determine whether the old header blocker is cleared and what modern kernel API errors remain.

See `docs/videobuf2-port-status.md`.

## Next test command

From the repo root:

```bash
git pull
git switch cachyos-driver-foundation
make clean-cachyos
make cachyos
```
