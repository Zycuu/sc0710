/*
 *  Driver for the Elgato 4k60 Pro mk.2 HDMI capture card.
 *
 *  Copyright (c) 2021-2022 Steven Toth <stoth@kernellabs.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/poll.h>

#include "sc0710.h"

static int video_debug = 1;

#define dprintk(level, fmt, arg...)\
	do { if (video_debug >= level)\
		printk(KERN_DEBUG "%s: " fmt, dev->name, ## arg);\
	} while (0)

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,14,0)
static void sc0710_vid_timeout(unsigned long data);
#else
static void sc0710_vid_timeout(struct timer_list *t);
#endif

const char *sc0710_colorimetry_ascii(enum sc0710_colorimetry_e val)
{
	switch (val) {
	case BT_601:       return "BT_601";
	case BT_709:       return "BT_709";
	case BT_2020:      return "BT_2020";
	default:           return "BT_UNDEFINED";
	}
}

const char *sc0710_colorspace_ascii(enum sc0710_colorspace_e val)
{
	switch (val) {
	case CS_YUV_YCRCB_422_420: return "YUV YCrCb 4:2:2 / 4:2:0";
	case CS_YUV_YCRCB_444:     return "YUV YCrCb 4:4:4";
	case CS_RGB_444:           return "RGB 4:4:4";
	default:                   return "UNDEFINED";
	}
}

#define FILL_MODE_COLORBARS 0
#define FILL_MODE_GREENSCREEN 1
#define FILL_MODE_BLUESCREEN 2
#define FILL_MODE_BLACKSCREEN 3
#define FILL_MODE_REDSCREEN 4

/* 75% IRE colorbars */
static unsigned char colorbars[7][4] =
{
	{ 0xc0, 0x80, 0xc0, 0x80 },
	{ 0xaa, 0x20, 0xaa, 0x8f },
	{ 0x86, 0xa0, 0x86, 0x20 },
	{ 0x70, 0x40, 0x70, 0x2f },
	{ 0x4f, 0xbf, 0x4f, 0xd0 },
	{ 0x39, 0x5f, 0x39, 0xe0 },
	{ 0x15, 0xe0, 0x15, 0x70 }
};
static unsigned char blackscreen[4] = { 0x00, 0x80, 0x00, 0x80 };
static unsigned char bluescreen[4] = { 0x1d, 0xff, 0x1d, 0x6b };
static unsigned char redscreen[4] = { 0x39, 0x5f, 0x39, 0xe0 };

static void fill_frame(struct sc0710_dma_channel *ch,
	unsigned char *dest_frame, unsigned int width,
	unsigned int height, unsigned int fillmode)
{
	unsigned int width_bytes = width * 2;
	unsigned int i, divider;

	if (fillmode > FILL_MODE_REDSCREEN)
		fillmode = FILL_MODE_BLACKSCREEN;

	switch (fillmode) {
	case FILL_MODE_COLORBARS:
		divider = (width_bytes / 7) + 1;
		for (i = 0; i < width_bytes; i += 4)
			memcpy(&dest_frame[i], &colorbars[i / divider], 4);
		break;
	case FILL_MODE_GREENSCREEN:
		memset(dest_frame, 0, width_bytes);
		break;
	case FILL_MODE_BLUESCREEN:
		for (i = 0; i < width_bytes; i += 4)
			memcpy(&dest_frame[i], bluescreen, 4);
		break;
	case FILL_MODE_REDSCREEN:
		for (i = 0; i < width_bytes; i += 4)
			memcpy(&dest_frame[i], redscreen, 4);
		break;
	case FILL_MODE_BLACKSCREEN:
		for (i = 0; i < width_bytes; i += 4)
			memcpy(&dest_frame[i], blackscreen, 4);
	}

	for (i = 1; i < height; i++) {
		memcpy(dest_frame + width_bytes, dest_frame, width_bytes);
		dest_frame += width_bytes;
	}
}

#if LINUX_VERSION_CODE <= KERNEL_VERSION(4, 0, 0)
/* Let's assume these appeared in v4.0 */

#define V4L2_DV_FL_IS_CE_VIDEO			(1 << 4)
#define V4L2_DV_FL_HAS_CEA861_VIC		(1 << 7)
#define V4L2_DV_FL_HAS_HDMI_VIC			(1 << 8)

#define V4L2_DV_BT_CEA_3840X2160P24 { \
	.type = V4L2_DV_BT_656_1120, \
	V4L2_INIT_BT_TIMINGS(3840, 2160, 0, \
		V4L2_DV_HSYNC_POS_POL | V4L2_DV_VSYNC_POS_POL, \
		297000000, 1276, 88, 296, 8, 10, 72, 0, 0, 0, \
		V4L2_DV_FL_CAN_REDUCE_FPS | V4L2_DV_FL_IS_CE_VIDEO | \
		V4L2_DV_FL_HAS_CEA861_VIC | V4L2_DV_FL_HAS_HDMI_VIC), \
}

#define V4L2_DV_BT_CEA_3840X2160P25 { \
	.type = V4L2_DV_BT_656_1120, \
	V4L2_INIT_BT_TIMINGS(3840, 2160, 0, \
		V4L2_DV_HSYNC_POS_POL | V4L2_DV_VSYNC_POS_POL, \
		297000000, 1056, 88, 296, 8, 10, 72, 0, 0, 0, \
		V4L2_DV_FL_IS_CE_VIDEO | V4L2_DV_FL_HAS_CEA861_VIC | \
		V4L2_DV_FL_HAS_HDMI_VIC), \
}

#define V4L2_DV_BT_CEA_3840X2160P30 { \
	.type = V4L2_DV_BT_656_1120, \
	V4L2_INIT_BT_TIMINGS(3840, 2160, 0, \
		V4L2_DV_HSYNC_POS_POL | V4L2_DV_VSYNC_POS_POL, \
		297000000, 176, 88, 296, 8, 10, 72, 0, 0, 0, \
		V4L2_DV_FL_CAN_REDUCE_FPS | V4L2_DV_FL_IS_CE_VIDEO | \
		V4L2_DV_FL_HAS_CEA861_VIC | V4L2_DV_FL_HAS_HDMI_VIC, \
		) \
}

#define V4L2_DV_BT_CEA_3840X2160P50 { \
	.type = V4L2_DV_BT_656_1120, \
	V4L2_INIT_BT_TIMINGS(3840, 2160, 0, \
		V4L2_DV_HSYNC_POS_POL | V4L2_DV_VSYNC_POS_POL, \
		594000000, 1056, 88, 296, 8, 10, 72, 0, 0, 0, \
		V4L2_DV_FL_IS_CE_VIDEO | V4L2_DV_FL_HAS_CEA861_VIC, ) \
}

#define V4L2_DV_BT_CEA_3840X2160P60 { \
	.type = V4L2_DV_BT_656_1120, \
	V4L2_INIT_BT_TIMINGS(3840, 2160, 0, \
		V4L2_DV_HSYNC_POS_POL | V4L2_DV_VSYNC_POS_POL, \
		594000000, 176, 88, 296, 8, 10, 72, 0, 0, 0, \
		V4L2_DV_FL_CAN_REDUCE_FPS | V4L2_DV_FL_IS_CE_VIDEO | \
		V4L2_DV_FL_HAS_CEA861_VIC,) \
}
#endif /* #if LINUX_VERSION_CODE <= KERNEL_VERSION(4, 0, 0) */

#define SUPPORT_INTERLACED 0
static struct sc0710_format formats[] =
{
#if SUPPORT_INTERLACED
	{  858,  262,  720,  240, 1, 2997, 30000, 1001, 8, 0, "720x480i29.97",   V4L2_DV_BT_CEA_720X480I59_94 },
#endif
	{  858,  525,  720,  480, 0, 5994, 60000, 1001, 8, 0, "720x480p59.94",   V4L2_DV_BT_CEA_720X480P59_94 },

#if SUPPORT_INTERLACED
	{  864,  312,  720,  288, 1, 2500, 25000, 1000, 8, 0, "720x576i25",      V4L2_DV_BT_CEA_720X576I50 },
#endif

	{ 1980,  750, 1280,  720, 0, 5000, 50000, 1000, 8, 0, "1280x720p50",     V4L2_DV_BT_CEA_1280X720P50 },
	{ 1650,  750, 1280,  720, 0, 5994, 60000, 1001, 8, 0, "1280x720p59.94",  V4L2_DV_BT_CEA_1280X720P60 },
	{ 1650,  750, 1280,  720, 0, 6000, 60000, 1000, 8, 0, "1280x720p60",     V4L2_DV_BT_CEA_1280X720P60 },

#if SUPPORT_INTERLACED
	{ 2640,  562, 1920,  540, 1, 2500, 25000, 1000, 8, 0, "1920x1080i25",    V4L2_DV_BT_CEA_1920X1080I50 },
	{ 2200,  562, 1920,  540, 1, 2997, 30000, 1001, 8, 0, "1920x1080i29.97", V4L2_DV_BT_CEA_1920X1080I60 },
#endif
	{ 2750, 1125, 1920, 1080, 0, 2400, 24000, 1000, 8, 0, "1920x1080p24",    V4L2_DV_BT_CEA_1920X1080P24 },
	{ 2640, 1125, 1920, 1080, 0, 2500, 25000, 1000, 8, 0, "1920x1080p25",    V4L2_DV_BT_CEA_1920X1080P25 },
	{ 2200, 1125, 1920, 1080, 0, 3000, 30000, 1000, 8, 0, "1920x1080p30",    V4L2_DV_BT_CEA_1920X1080P30 },
	{ 2640, 1125, 1920, 1080, 0, 5000, 50000, 1000, 8, 0, "1920x1080p50",    V4L2_DV_BT_CEA_1920X1080P50 },
	{ 2200, 1125, 1920, 1080, 0, 6000, 60000, 1000, 8, 0, "1920x1080p60",    V4L2_DV_BT_CEA_1920X1080P60 },

	{ 4400, 2250, 3840, 2160, 0, 6000, 60000, 1000, 8, 0, "3840x2160p60",    V4L2_DV_BT_CEA_3840X2160P60 },
};

void sc0710_format_initialize(void)
{
	struct sc0710_format *fmt;
	unsigned int i;
	for (i = 0; i < ARRAY_SIZE(formats); i++) {
		fmt = &formats[i];

		/* Assuming YUV 8-bit */
		fmt->framesize = fmt->width * 2 * fmt->height;
	}
}

const struct sc0710_format *sc0710_format_find_by_timing(u32 timingH, u32 timingV)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(formats); i++) {
		if ((formats[i].timingH == timingH) && (formats[i].timingV == timingV)) {
			return &formats[i];
		}
	}

	return NULL;
}

static int vidioc_s_dv_timings(struct file *file, void *_fh, struct v4l2_dv_timings *timings)
{
	struct sc0710_dma_channel *ch = video_drvdata(file);
	struct sc0710_dev *dev = ch->dev;

	dprintk(1, "%s()\n", __func__);

	return -EINVAL; /* No support for setting DV Timings */
}

static int vidioc_g_dv_timings(struct file *file, void *_fh, struct v4l2_dv_timings *timings)
{
	struct sc0710_dma_channel *ch = video_drvdata(file);
	struct sc0710_dev *dev = ch->dev;

	dprintk(0, "%s()\n", __func__);

	if (dev->fmt == NULL)
		return -EINVAL;

	*timings = dev->fmt->dv_timings;
	return 0;
}

static int vidioc_query_dv_timings(struct file *file, void *_fh, struct v4l2_dv_timings *timings)
{
	return vidioc_g_dv_timings(file, _fh, timings);
}

static int vidioc_enum_dv_timings(struct file *file, void *_fh, struct v4l2_enum_dv_timings *timings)
{
	memset(timings->reserved, 0, sizeof(timings->reserved));

	if (timings->index >= ARRAY_SIZE(formats))
		return -EINVAL;

	timings->timings = formats[timings->index].dv_timings;
	return 0;
}

static int vidioc_dv_timings_cap(struct file *file, void *_fh, struct v4l2_dv_timings_cap *cap)
{
	memset(cap, 0, sizeof(*cap));
	cap->type = V4L2_DV_BT_656_1120;
	cap->bt.min_width = 720;
	cap->bt.max_width = 3840;
	cap->bt.min_height = 480;
	cap->bt.max_height = 2160;
	cap->bt.min_pixelclock = 27000000;
	cap->bt.max_pixelclock = 594000000;
	cap->bt.standards = V4L2_DV_BT_STD_CEA861;
	cap->bt.capabilities = V4L2_DV_BT_CAP_PROGRESSIVE;
#if SUPPORT_INTERLACED
	cap->bt.capabilities |= V4L2_DV_BT_CAP_INTERLACED;
#endif

	return 0;
}

static void sc0710_fill_pix_format(struct sc0710_dev *dev, struct v4l2_pix_format *pix)
{
	const struct sc0710_format *fmt = dev->fmt ? dev->fmt : &formats[0];

	pix->width = fmt->width;
	pix->height = fmt->height;
	pix->pixelformat = V4L2_PIX_FMT_YUYV;
	pix->field = V4L2_FIELD_NONE;
	pix->bytesperline = fmt->width * 2;
	pix->sizeimage = fmt->framesize;
	pix->colorspace = V4L2_COLORSPACE_REC709;
}

static int vidioc_querycap(struct file *file, void *priv, struct v4l2_capability *cap)
{
	struct sc0710_dma_channel *ch = video_drvdata(file);
	struct sc0710_dev *dev = ch->dev;

	strscpy(cap->driver, "sc0710", sizeof(cap->driver));
	strscpy(cap->card, sc0710_boards[dev->board].name, sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info), "PCIe:%s", pci_name(dev->pci));

	cap->device_caps = V4L2_CAP_READWRITE | V4L2_CAP_STREAMING | V4L2_CAP_AUDIO |
		V4L2_CAP_VIDEO_CAPTURE;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;

	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void *priv, struct v4l2_fmtdesc *f)
{
	if (f->index != 0)
		return -EINVAL;

	f->pixelformat = V4L2_PIX_FMT_YUYV;
	strscpy(f->description, "YUYV 4:2:2", sizeof(f->description));
	return 0;
}

static int vidioc_g_fmt_vid_cap(struct file *file, void *priv, struct v4l2_format *f)
{
	struct sc0710_dma_channel *ch = video_drvdata(file);
	struct sc0710_dev *dev = ch->dev;

	sc0710_fill_pix_format(dev, &f->fmt.pix);
	return 0;
}

static int vidioc_try_fmt_vid_cap(struct file *file, void *priv, struct v4l2_format *f)
{
	struct sc0710_dma_channel *ch = video_drvdata(file);
	struct sc0710_dev *dev = ch->dev;

	sc0710_fill_pix_format(dev, &f->fmt.pix);
	return 0;
}

static int vidioc_s_fmt_vid_cap(struct file *file, void *priv, struct v4l2_format *f)
{
	return vidioc_try_fmt_vid_cap(file, priv, f);
}

static int vidioc_enum_input(struct file *file, void *priv, struct v4l2_input *i)
{
	struct sc0710_dma_channel *ch = video_drvdata(file);
	struct sc0710_dev *dev = ch->dev;
	dprintk(1, "%s()\n", __func__);

	if (i->index != 0)
		return -EINVAL;

	i->type  = V4L2_INPUT_TYPE_CAMERA;
	strscpy(i->name, "HDMI", sizeof(i->name));

	return 0;
}

static int vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	struct sc0710_dma_channel *ch = video_drvdata(file);
	struct sc0710_dev *dev = ch->dev;

	dprintk(1, "%s(%d)\n", __func__, i);
	return i ? -EINVAL : 0;
}

static int vidioc_g_input(struct file *file, void *priv, unsigned int *i)
{
	struct sc0710_dma_channel *ch = video_drvdata(file);
	struct sc0710_dev *dev = ch->dev;
	dprintk(1, "%s()\n", __func__);

	*i = 0;
	return 0;
}

static void sc0710_return_all_buffers(struct sc0710_dma_channel *ch, enum vb2_buffer_state state)
{
	struct sc0710_buffer *buf;
	unsigned long flags;

	spin_lock_irqsave(&ch->v4l2_capture_list_lock, flags);
	while (!list_empty(&ch->v4l2_capture_list)) {
		buf = list_first_entry(&ch->v4l2_capture_list, struct sc0710_buffer, list);
		list_del(&buf->list);
		spin_unlock_irqrestore(&ch->v4l2_capture_list_lock, flags);

		vb2_buffer_done(&buf->vb.vb2_buf, state);

		spin_lock_irqsave(&ch->v4l2_capture_list_lock, flags);
	}
	spin_unlock_irqrestore(&ch->v4l2_capture_list_lock, flags);
}

static int sc0710_queue_setup(struct vb2_queue *q,
	unsigned int *count, unsigned int *num_planes,
	unsigned int sizes[], struct device *alloc_devs[])
{
	struct sc0710_dma_channel *ch = q->drv_priv;
	struct sc0710_dev *dev = ch->dev;
	unsigned int size;

	if (!dev->fmt)
		return -EINVAL;

	size = dev->fmt->framesize;
	if (*num_planes) {
		if (sizes[0] < size)
			return -EINVAL;
		return 0;
	}

	if (*count == 0)
		*count = 8;

	*num_planes = 1;
	sizes[0] = size;
	return 0;
}

static int sc0710_buf_prepare(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct sc0710_buffer *buf = container_of(vbuf, struct sc0710_buffer, vb);
	struct sc0710_dma_channel *ch = vb->vb2_queue->drv_priv;
	struct sc0710_dev *dev = ch->dev;
	unsigned int size;

	if (!dev->fmt)
		return -EINVAL;

	size = dev->fmt->framesize;
	if (vb2_plane_size(vb, 0) < size)
		return -EINVAL;

	buf->fmt = dev->fmt;
	vb2_set_plane_payload(vb, 0, 0);
	return 0;
}

static void sc0710_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct sc0710_buffer *buf = container_of(vbuf, struct sc0710_buffer, vb);
	struct sc0710_dma_channel *ch = vb->vb2_queue->drv_priv;
	unsigned long flags;

	spin_lock_irqsave(&ch->v4l2_capture_list_lock, flags);
	list_add_tail(&buf->list, &ch->v4l2_capture_list);
	spin_unlock_irqrestore(&ch->v4l2_capture_list_lock, flags);
}

static int sc0710_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct sc0710_dma_channel *ch = q->drv_priv;
	struct sc0710_dev *dev = ch->dev;

	dprintk(1, "%s(ch#%d)\n", __func__, ch->nr);

	if (!dev->fmt)
		goto fail;

	if (sc0710_dma_channels_resize(dev) < 0)
		goto fail;

	if (sc0710_dma_channels_start(dev) < 0)
		goto fail;

	mod_timer(&ch->timeout, jiffies + VBUF_TIMEOUT);
	return 0;

fail:
	sc0710_return_all_buffers(ch, VB2_BUF_STATE_QUEUED);
	return -EINVAL;
}

static void sc0710_stop_streaming(struct vb2_queue *q)
{
	struct sc0710_dma_channel *ch = q->drv_priv;
	struct sc0710_dev *dev = ch->dev;

	dprintk(1, "%s(ch#%d)\n", __func__, ch->nr);

	del_timer_sync(&ch->timeout);
	sc0710_dma_channels_stop(dev);
	sc0710_return_all_buffers(ch, VB2_BUF_STATE_ERROR);
}

static const struct vb2_ops sc0710_video_qops =
{
	.queue_setup     = sc0710_queue_setup,
	.buf_prepare     = sc0710_buf_prepare,
	.buf_queue       = sc0710_buf_queue,
	.start_streaming = sc0710_start_streaming,
	.stop_streaming  = sc0710_stop_streaming,
	.wait_prepare    = vb2_ops_wait_prepare,
	.wait_finish     = vb2_ops_wait_finish,
};

static int sc0710_video_open(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct sc0710_dma_channel *ch = video_drvdata(file);
	struct sc0710_dev *dev = ch->dev;
	struct sc0710_fh *fh;

	dprintk(0, "%s() dev=%s\n", __func__, video_device_node_name(vdev));

	fh = kzalloc(sizeof(*fh), GFP_KERNEL);
	if (fh == NULL)
		return -ENOMEM;

	fh->ch   = ch;
	fh->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	v4l2_fh_init(&fh->fh, vdev);
	file->private_data = fh;
	v4l2_fh_add(&fh->fh);

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,14,0)
	init_timer(&ch->timeout);
	ch->timeout.function = sc0710_vid_timeout;
	ch->timeout.data     = (unsigned long)ch;
#else
	timer_setup(&ch->timeout, sc0710_vid_timeout, 0);
#endif

	mutex_lock(&ch->lock);
	ch->videousers++;
	mutex_unlock(&ch->lock);

	return 0;
}

static int sc0710_video_release(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct sc0710_fh *fh = file->private_data;
	struct sc0710_dma_channel *ch = fh->ch;
	struct sc0710_dev *dev = ch->dev;
	bool last_user = false;

	dprintk(1, "%s() dev=%s\n", __func__, video_device_node_name(vdev));

	mutex_lock(&ch->lock);
	if (ch->videousers)
		ch->videousers--;
	last_user = (ch->videousers == 0);
	mutex_unlock(&ch->lock);

	if (last_user)
		vb2_queue_release(&ch->vb2_queue);

	v4l2_fh_del(&fh->fh);
	v4l2_fh_exit(&fh->fh);
	file->private_data = NULL;
	kfree(fh);

	return 0;
}

static const struct v4l2_file_operations video_fops = {
	.owner	        = THIS_MODULE,
	.open           = sc0710_video_open,
	.release        = sc0710_video_release,
	.read           = vb2_fop_read,
	.poll	        = vb2_fop_poll,
	.mmap           = vb2_fop_mmap,
	.unlocked_ioctl = video_ioctl2,
};

static const struct v4l2_ioctl_ops video_ioctl_ops =
{
	.vidioc_querycap         = vidioc_querycap,

	.vidioc_enum_fmt_vid_cap = vidioc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap    = vidioc_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap  = vidioc_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap    = vidioc_s_fmt_vid_cap,

	.vidioc_s_dv_timings     = vidioc_s_dv_timings,
	.vidioc_g_dv_timings     = vidioc_g_dv_timings,
	.vidioc_query_dv_timings = vidioc_query_dv_timings,
	.vidioc_enum_dv_timings  = vidioc_enum_dv_timings,
	.vidioc_dv_timings_cap   = vidioc_dv_timings_cap,

	.vidioc_enum_input       = vidioc_enum_input,
	.vidioc_g_input          = vidioc_g_input,
	.vidioc_s_input          = vidioc_s_input,

	.vidioc_reqbufs          = vb2_ioctl_reqbufs,
	.vidioc_querybuf         = vb2_ioctl_querybuf,
	.vidioc_qbuf             = vb2_ioctl_qbuf,
	.vidioc_dqbuf            = vb2_ioctl_dqbuf,
	.vidioc_streamon         = vb2_ioctl_streamon,
	.vidioc_streamoff        = vb2_ioctl_streamoff,
};

static struct video_device sc0710_video_template =
{
	.name      = "sc0710-video",
	.fops      = &video_fops,
	.ioctl_ops = &video_ioctl_ops,
};

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,14,0)
static void sc0710_vid_timeout(unsigned long data)
{
	struct sc0710_dma_channel *ch = (struct sc0710_dma_channel *)data;
#else
static void sc0710_vid_timeout(struct timer_list *t)
{
	struct sc0710_dma_channel *ch = from_timer(ch, t, timeout);
#endif
	struct sc0710_dev *dev = ch->dev;
	struct sc0710_buffer *buf;
	unsigned long flags;
	u8 *dst;
	unsigned int len;

	dprintk(0, "%s(ch#%d)\n", __func__, ch->nr);

	spin_lock_irqsave(&ch->v4l2_capture_list_lock, flags);
	while (!list_empty(&ch->v4l2_capture_list)) {
		buf = list_first_entry(&ch->v4l2_capture_list, struct sc0710_buffer, list);
		list_del(&buf->list);
		spin_unlock_irqrestore(&ch->v4l2_capture_list_lock, flags);

		dst = vb2_plane_vaddr(&buf->vb.vb2_buf, 0);
		if (dst && dev->fmt) {
			len = min_t(unsigned int, dev->fmt->framesize,
				vb2_plane_size(&buf->vb.vb2_buf, 0));
			fill_frame(ch, dst, dev->fmt->width, dev->fmt->height, FILL_MODE_COLORBARS);
			vb2_set_plane_payload(&buf->vb.vb2_buf, 0, len);
			buf->vb.vb2_buf.timestamp = ktime_get_ns();
			vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
		} else {
			vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
		}

		spin_lock_irqsave(&ch->v4l2_capture_list_lock, flags);
	}
	spin_unlock_irqrestore(&ch->v4l2_capture_list_lock, flags);

	if (sc0710_dma_channel_state(ch) == STATE_RUNNING)
		mod_timer(&ch->timeout, jiffies + VBUF_TIMEOUT);
}

void sc0710_video_unregister(struct sc0710_dma_channel *ch)
{
	struct sc0710_dev *dev = ch->dev;

	dprintk(1, "%s()\n", __func__);

	if (video_is_registered(&ch->vdev))
		video_unregister_device(&ch->vdev);
	else
		video_device_release(&ch->vdev);
}

int sc0710_video_register(struct sc0710_dma_channel *ch)
{
	struct sc0710_dev *dev = ch->dev;
	int err;
	struct vb2_queue *q = &ch->vb2_queue;

	memset(q, 0, sizeof(*q));
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_READ;
	q->drv_priv = ch;
	q->buf_struct_size = sizeof(struct sc0710_buffer);
	q->ops = &sc0710_video_qops;
	q->mem_ops = &vb2_vmalloc_memops;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->lock = &ch->lock;
	q->dev = &dev->pci->dev;

	spin_lock_init(&ch->slock);

	err = vb2_queue_init(q);
	if (err) {
		printk(KERN_INFO "%s: can't initialize vb2 queue\n", dev->name);
		return err;
	}

	memcpy(&ch->vdev, &sc0710_video_template, sizeof(sc0710_video_template));
	ch->vdev.lock = &ch->lock;
	ch->vdev.release = video_device_release_empty;
	ch->vdev.vfl_dir = VFL_DIR_RX;
	ch->vdev.queue = q;
	ch->vdev.device_caps = V4L2_CAP_STREAMING | V4L2_CAP_READWRITE | V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_AUDIO;
	ch->vdev.v4l2_dev = &dev->v4l2_dev;

#if LINUX_VERSION_CODE <= KERNEL_VERSION(4,0,0)
	ch->v4l_device->parent = &dev->pci->dev;
#else
	ch->vdev.dev_parent = &dev->pci->dev;
#endif
	strscpy(ch->vdev.name, "sc0710 video", sizeof(ch->vdev.name));

	video_set_drvdata(&ch->vdev, ch);

	err = video_register_device(&ch->vdev,
#if LINUX_VERSION_CODE <= KERNEL_VERSION(4,0,0)
		VFL_TYPE_GRABBER,
#else
		VFL_TYPE_VIDEO,
#endif
		-1);
	if (err < 0) {
		printk(KERN_INFO "%s: can't register video device\n", dev->name);
		return err;
	}

	printk(KERN_INFO "%s: registered device %s [v4l2]\n",
	       dev->name, video_device_node_name(&ch->vdev));

	return 0; /* Success */
}
