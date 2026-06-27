# CachyOS build notes

## First observed failure

System:

```text
/lib/modules/7.0.11-1-cachyos/build
```

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

## Meaning

This is a toolchain mismatch. CachyOS built the running kernel with Clang, but the external module build tried to use GCC. The kernel build directory exports compiler flags that are valid for Clang and LLVM, so GCC fails before the driver source is meaningfully compiled.

This does not yet prove that the driver works or fails on CachyOS. It only means the build never reached the real driver compatibility problems.

## Fix added in this fork branch

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

## Next test command

From the repo root:

```bash
git pull
git switch cachyos-driver-foundation
make clean-cachyos
make cachyos
```

## Expected next phase

After the Clang versus GCC mismatch is resolved, the next likely failures will probably come from driver source compatibility with modern kernel APIs. Based on earlier source review, the video path still uses older `videobuf` calls in active paths, so the next main repair area is expected to be the V4L2 buffer handling path.
