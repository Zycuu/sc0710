# V4L2 videobuf2 port plan

## Current failure

CachyOS 7.0.11 fails here:

```text
./sc0710.h:54:10: fatal error: 'media/videobuf-vmalloc.h' file not found
```

That means the driver has reached the real modern kernel compatibility problem. The old `videobuf` interface used by this source is not available in the CachyOS kernel headers.

## Why a small include fix is not enough

The missing include is only the first visible symptom. The driver still uses legacy videobuf structures and functions in active runtime paths.

Examples in the current source:

- `struct sc0710_buffer` embeds `struct videobuf_buffer`.
- `struct sc0710_fh` contains `struct videobuf_queue vidq`.
- `sc0710_video_open()` calls `videobuf_queue_vmalloc_init()`.
- V4L2 ioctl handlers call `videobuf_reqbufs()`, `videobuf_querybuf()`, `videobuf_qbuf()`, `videobuf_dqbuf()`, `videobuf_streamon()`, and `videobuf_streamoff()`.
- `read()`, `poll()`, and `mmap()` call `videobuf_read_one()`, `videobuf_poll_stream()`, and `videobuf_mmap_mapper()`.
- The DMA path calls `videobuf_to_vmalloc()`, updates `VIDEOBUF_*` states, removes the old buffer list entry, and wakes `vb.done`.

So the correct fix is to port the capture path to videobuf2 rather than trying to recreate the removed legacy API.

## Design target

Keep the device visible as a normal V4L2 capture node:

```text
/dev/videoX
```

Keep user space compatibility for common tools:

```bash
v4l2-ctl --device=/dev/video0 --all
ffmpeg -f v4l2 -i /dev/video0 ...
OBS Studio video capture device source
```

## Old path to new path mapping

| Old videobuf item | videobuf2 replacement direction |
| --- | --- |
| `struct videobuf_buffer` | `struct vb2_v4l2_buffer` plus driver private wrapper |
| `struct videobuf_queue` | `struct vb2_queue` |
| `videobuf_queue_vmalloc_init()` | `vb2_queue_init()` with `vb2_vmalloc_memops` or DMA capable memops |
| `videobuf_reqbufs()` | `vb2_ioctl_reqbufs()` through V4L2 ioctl ops |
| `videobuf_querybuf()` | `vb2_ioctl_querybuf()` |
| `videobuf_qbuf()` | `vb2_ioctl_qbuf()` |
| `videobuf_dqbuf()` | `vb2_ioctl_dqbuf()` |
| `videobuf_streamon()` | `vb2_ioctl_streamon()` and `start_streaming` callback |
| `videobuf_streamoff()` | `vb2_ioctl_streamoff()` and `stop_streaming` callback |
| `videobuf_read_one()` | `vb2_fop_read` |
| `videobuf_poll_stream()` | `vb2_fop_poll` |
| `videobuf_mmap_mapper()` | `vb2_fop_mmap` |
| `videobuf_to_vmalloc()` | `vb2_plane_vaddr()` |
| `VIDEOBUF_DONE` | `vb2_buffer_done(..., VB2_BUF_STATE_DONE)` |
| `VIDEOBUF_ERROR` | `vb2_buffer_done(..., VB2_BUF_STATE_ERROR)` |

## Proposed struct changes

`struct sc0710_buffer` should become something like:

```c
struct sc0710_buffer {
        struct vb2_v4l2_buffer vb;
        struct list_head list;
        const struct sc0710_format *fmt;
};
```

`struct sc0710_fh` should stop carrying a per-file legacy videobuf queue. The channel already has `struct vb2_queue vb2_queue`, so the file handle can keep only the V4L2 file handle, channel pointer, resources, and type.

## Proposed callback skeleton

The new `struct vb2_ops` needs at least:

- `queue_setup`
- `buf_prepare`
- `buf_queue`
- `start_streaming`
- `stop_streaming`

The driver already has a queued buffer list and a DMA polling thread. That means the port should preserve the existing list based handoff, but list nodes should be owned by the new driver wrapper instead of the old `videobuf_buffer` queue member.

## DMA handoff change

Current DMA handoff:

```c
vb_buf = list_entry(ch->v4l2_capture_list.next, struct sc0710_buffer, vb.queue);
dst = videobuf_to_vmalloc(&vb_buf->vb);
len = sc0710_dma_chain_dq_to_ptr(ch, chain, dst, vb_buf->vb.size);
vb_buf->vb.state = VIDEOBUF_DONE;
wake_up(&vb_buf->vb.done);
```

Target DMA handoff:

```c
vb_buf = list_first_entry(&ch->v4l2_capture_list, struct sc0710_buffer, list);
dst = vb2_plane_vaddr(&vb_buf->vb.vb2_buf, 0);
len = sc0710_dma_chain_dq_to_ptr(ch, chain, dst, dev->fmt->framesize);
vb2_set_plane_payload(&vb_buf->vb.vb2_buf, 0, len);
vb_buf->vb.vb2_buf.timestamp = ktime_get_ns();
list_del(&vb_buf->list);
vb2_buffer_done(&vb_buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
```

This is the main mechanical port.

## Order of work

1. Convert the data structures in `sc0710.h`.
2. Replace legacy buffer queue callbacks in `sc0710-video.c` with `vb2_ops`.
3. Replace V4L2 ioctl handlers with `vb2_ioctl_*` helpers.
4. Replace file operations with `vb2_fop_read`, `vb2_fop_poll`, and `vb2_fop_mmap`.
5. Convert DMA completion to `vb2_plane_vaddr()`, `vb2_set_plane_payload()`, and `vb2_buffer_done()`.
6. Confirm the module builds.
7. Only then test device registration, load, `/dev/videoX`, and capture.

## Important warning

This is likely the largest code change in the fork. A compatibility shim for `videobuf-vmalloc.h` would hide the first error but would not solve the removed API surface.
