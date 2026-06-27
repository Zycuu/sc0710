# CachyOS build notes

## Test system

```text
/lib/modules/7.0.11-1-cachyos/build
```

## First observed failure

The first build failure was:

```text
warning: the compiler differs from the one used to build the kernel
  The kernel was built by: clang version 22.1.6
  You are using:           gcc (GCC) 16.1.1 20260430

gcc: error: unrecognized command-line option '-mstack-alignment=8'
gcc: error: unrecognized command-line option '-mretpoline-external-thunk'
gcc: error: unrecognized command-line option '-fexperimental-late-parse-attributes'
gcc: error: unrecognized command-line option '-fsplit-lto-unit'
gcc: error: unrecognized command-line option '-fdebug-info-for-profiling'
gcc: error: unrecognized command-line option '-mllvm'
gcc: error: unrecognized command-line option '-improved-fs-discriminator=true'
```

### Meaning

This was a toolchain mismatch. CachyOS built the running kernel with Clang, but the external module build tried to use GCC. The kernel build directory exports compiler flags that are valid for Clang and LLVM, so GCC fails before the driver source is meaningfully compiled.

### Fix added in this fork branch

The Makefile now supports these targets:

```bash
make cachyos
make clean-cachyos
```

Those targets pass `LLVM=1` into kbuild.

Equivalent manual command:

```bash
make KBUILD_FLAGS="LLVM=1"
```

## Second observed failure

After rebuilding with `LLVM=1`, the build reached the real source compatibility issue:

```text
CC [M]  sc0710-cards.o
In file included from sc0710-cards.c:21:
./sc0710.h:54:10: fatal error: 'media/videobuf-vmalloc.h' file not found
   54 | #include <media/videobuf-vmalloc.h>
      |          ^~~~~~
1 error generated.
```

### Meaning

This confirms the current CachyOS kernel headers do not provide the legacy `videobuf-vmalloc` interface used by the driver.

This is not a missing user package in the normal sense. The driver source depends on an old V4L buffer API that needs to be ported to videobuf2.

## Current next step

See:

```text
docs/videobuf2-port-plan.md
```

The next code branch should replace the legacy videobuf capture path with videobuf2 callbacks and helpers.

## Next test command after the port starts

From the repo root:

```bash
git pull
git switch cachyos-driver-foundation
make clean-cachyos
make cachyos
```
