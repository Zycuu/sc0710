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

#include "sc0710.h"

static int dma_channel_debug = 1;
#define dprintk(level, fmt, arg...)\
	do { if (dma_channel_debug >= level)\
		printk(KERN_DEBUG "%s: " fmt, dev->name, ## arg);\
	} while (0)

#define DMA_AUDIO_TRANSFER_SIZE 0x4000
#define DMA_TRANSFER_CHAINS     4

/* Copy the contents of a completed video DMA chain into the next queued vb2 buffer. */
static void sc0710_dma_dequeue_video(struct sc0710_dma_channel *ch, struct sc0710_dma_descriptor_chain *chain)
{
	struct sc0710_dev *dev = ch->dev;
	struct sc0710_buffer *vb_buf = NULL;
	unsigned long flags;
	u8 *dst = NULL;
	unsigned int dstlen;
	int len;

	spin_lock_irqsave(&ch->v4l2_capture_list_lock, flags);
	if (list_empty(&ch->v4l2_capture_list)) {
		spin_unlock_irqrestore(&ch->v4l2_capture_list_lock, flags);
		return;
	}

	vb_buf = list_first_entry(&ch->v4l2_capture_list, struct sc0710_buffer, list);
	list_del(&vb_buf->list);
	spin_unlock_irqrestore(&ch->v4l2_capture_list_lock, flags);

	dst = vb2_plane_vaddr(&vb_buf->vb.vb2_buf, 0);
	if (!dst || !dev->fmt) {
		printk(KERN_ERR "%s() vb2 buffer not accessible\n", __func__);
		vb2_buffer_done(&vb_buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
		return;
	}

	dstlen = min_t(unsigned int, dev->fmt->framesize, vb2_plane_size(&vb_buf->vb.vb2_buf, 0));

	dprintk(3, "%s() copying %u bytes\n", __func__, dstlen);

	len = sc0710_dma_chain_dq_to_ptr(ch, chain, dst, dstlen);
	if (len != dstlen) {
		printk(KERN_ERR "%s() error copying %u bytes, copied %d\n", __func__, dstlen, len);
		vb2_set_plane_payload(&vb_buf->vb.vb2_buf, 0, max(len, 0));
		vb_buf->vb.vb2_buf.timestamp = ktime_get_ns();
		vb2_buffer_done(&vb_buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
		return;
	}

	vb2_set_plane_payload(&vb_buf->vb.vb2_buf, 0, len);
	vb_buf->vb.vb2_buf.timestamp = ktime_get_ns();
	vb2_buffer_done(&vb_buf->vb.vb2_buf, VB2_BUF_STATE_DONE);

	mod_timer(&ch->timeout, jiffies + VBUF_TIMEOUT);
}

/* Copy the contents of an audio DMA chain into the ALSA capture path. */
static void sc0710_dma_dequeue_audio(struct sc0710_dma_channel *ch, struct sc0710_dma_descriptor_chain *chain)
{
	struct sc0710_dev *dev = ch->dev;
	struct sc0710_dma_descriptor_chain_allocation *dca = &chain->allocations[0];
	int samplesPerChannel;
	int stride = 16;
	int ret;
	int i;

	if (chain->numAllocations != 1) {
		printk("%s() allocations should be one, dma issue?\n", __func__);
	}

	for (i = 0; i < chain->numAllocations; i++) {
		samplesPerChannel = dca->buf_size / stride;

		ret = sc0710_audio_deliver_samples(ch->dev, ch,
			(const u8 *)dca->buf_cpu,
			16,     /* bitwidth */
			stride,
			2,      /* channels */
			samplesPerChannel);
		if (ret < 0)
			dprintk(2, "%s() audio delivery failed %d\n", __func__, ret);

		dca++;
	}
}

int sc0710_dma_channel_service(struct sc0710_dma_channel *ch)
{
	struct sc0710_dev *dev = ch->dev;
	struct sc0710_dma_descriptor_chain_allocation *dca;
	struct sc0710_dma_descriptor_chain *chain;
	u32 wbm[2];
	u32 v;
	int i;

	if (ch->enabled == 0)
		return -1;

	v = sc_read(ch->dev, 1, ch->reg_dma_completed_descriptor_count);
	if (v == ch->dma_completed_descriptor_count_last) {
		return 0;
	}

	dprintk(3, "ch#%d    was %d now %d\n", ch->nr, ch->dma_completed_descriptor_count_last, v);
	ch->dma_completed_descriptor_count_last = v;

	for (i = 0; i < ch->numDescriptorChains; i++) {
		chain = &ch->chains[i];
		dca = &chain->allocations[ chain->numAllocations - 1 ];

		wbm[0] = *dca->wbm[0];
		wbm[1] = *dca->wbm[1];

		if (wbm[0] && wbm[1]) {
			if (dma_channel_debug > 2) {
				printk("%s ch#%d    [%02d] %08x - wbm %08x %08x (DQ) segs: %d\n",
					ch->dev->name,
					ch->nr,
					i,
					dca->desc->control,
					wbm[0],
					wbm[1], chain->numAllocations);
			}

			sc0710_things_per_second_update(&ch->bitsPerSecond, chain->total_transfer_size * 8);
			sc0710_things_per_second_update(&ch->descPerSecond, chain->numAllocations);

			if (ch->mediatype == CHTYPE_VIDEO) {
				sc0710_dma_dequeue_video(ch, chain);
			} else if (ch->mediatype == CHTYPE_AUDIO) {
				sc0710_dma_dequeue_audio(ch, chain);
			}

			*(dca->wbm[0]) = 0;
			*(dca->wbm[1]) = 0;
		}
	}

	return 0;
}

static int sc0710_dma_channel_chains_link(struct sc0710_dma_channel *ch)
{
	struct sc0710_dma_descriptor_chain *chain;
	struct sc0710_dma_descriptor_chain_allocation *dca;
	struct sc0710_dma_descriptor *pt_desc = (struct sc0710_dma_descriptor *)ch->pt_cpu;
	dma_addr_t curr_tbl = ch->pt_dma;
	dma_addr_t curr_wbm = ch->pt_dma + PAGE_SIZE;
	int i, j;

	for (i = 0; i < ch->numDescriptorChains; i++) {
		chain = &ch->chains[i];

		for (j = 0; j < chain->numAllocations; j++) {
			dca = &chain->allocations[j];
			dca->desc = pt_desc++;

			if ((i + 1 == ch->numDescriptorChains) && (j + 1 == chain->numAllocations)) {
				dca->desc->next_l = (u64)ch->pt_dma;
				dca->desc->next_h = (u64)ch->pt_dma >> 32;
			} else {
				dca->desc->next_l = (u64)curr_tbl + sizeof(struct sc0710_dma_descriptor);
				dca->desc->next_h = ((u64)curr_tbl + sizeof(struct sc0710_dma_descriptor)) >> 32;
			}

			dca->desc->control     = 0xAD4B0000;
			dca->desc->lengthBytes = dca->buf_size;
			dca->desc->src_l       = (u64)curr_wbm;
			dca->desc->src_h       = (u64)curr_wbm >> 32;
			dca->desc->dst_l       = (u64)dca->buf_dma;
			dca->desc->dst_h       = (u64)dca->buf_dma >> 32;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 18, 0)
			dca->wbm[0] = phys_to_virt(curr_wbm);
			dca->wbm[1] = phys_to_virt(curr_wbm) + sizeof(u32);
#else
			dca->wbm[0] = bus_to_virt(curr_wbm);
			dca->wbm[1] = bus_to_virt(curr_wbm) + sizeof(u32);
#endif

			curr_tbl += sizeof(struct sc0710_dma_descriptor);
			curr_wbm += sizeof(struct sc0710_dma_descriptor);
		}
	}

	return 0;
}

int sc0710_dma_channel_alloc(struct sc0710_dev *dev, u32 nr, enum sc0710_channel_dir_e direction,
	u32 baseaddr,
	enum sc0710_channel_type_e mediatype)
{
	int ret = 0;
	struct sc0710_dma_channel *ch = &dev->channel[nr];

	if (nr >= SC0710_MAX_CHANNELS)
		return -1;
	if (direction != CHDIR_INPUT)
		return -1;

	memset(ch, 0, sizeof(*ch));
	mutex_init(&ch->lock);
	spin_lock_init(&ch->v4l2_capture_list_lock);
	INIT_LIST_HEAD(&ch->v4l2_capture_list);

	ch->dev = dev;
	ch->nr = nr;
	ch->enabled = 1;
	ch->direction = direction;
	ch->mediatype = mediatype;
	ch->state = STATE_STOPPED;
	sc0710_things_per_second_reset(&ch->bitsPerSecond);
	sc0710_things_per_second_reset(&ch->descPerSecond);
	sc0710_things_per_second_reset(&ch->audioSamplesPerSecond);

	if (ch->mediatype == CHTYPE_VIDEO) {
		ch->numDescriptorChains = DMA_TRANSFER_CHAINS;
		ch->buf_size = 1280 * 2 * 720;
		printk("Allocating channel for size %d\n", ch->buf_size);
	} else if (ch->mediatype == CHTYPE_AUDIO) {
		ch->numDescriptorChains = DMA_TRANSFER_CHAINS;
		ch->buf_size = DMA_AUDIO_TRANSFER_SIZE;
	} else {
		ch->numDescriptorChains = 0;
	}

	ch->pt_size = PAGE_SIZE * 2;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 18, 0)
	ch->pt_cpu = dma_alloc_coherent(&((struct pci_dev *)dev->pci)->dev, ch->pt_size, &ch->pt_dma, GFP_ATOMIC);
#else
	ch->pt_cpu = pci_alloc_consistent(dev->pci, ch->pt_size, &ch->pt_dma);
#endif
	if (ch->pt_cpu == 0)
		return -1;

	memset(ch->pt_cpu, 0, ch->pt_size);

	ch->register_dma_base = baseaddr;
	ch->reg_dma_control = ch->register_dma_base + 0x04;
	ch->reg_dma_control_w1s = ch->register_dma_base + 0x08;
	ch->reg_dma_control_w1c = ch->register_dma_base + 0x0c;
	ch->reg_dma_status1 = ch->register_dma_base + 0x40;
	ch->reg_dma_status2 = ch->register_dma_base + 0x44;
	ch->reg_dma_completed_descriptor_count = ch->register_dma_base + 0x48;
	ch->reg_dma_poll_wba_l = ch->register_dma_base + 0x88;
	ch->reg_dma_poll_wba_h = ch->register_dma_base + 0x8c;

	ch->register_sg_base = baseaddr + 0x4000;
	ch->reg_sg_start_l = ch->register_sg_base + 0x80;
	ch->reg_sg_start_h = ch->register_sg_base + 0x84;
	ch->reg_sg_adj = ch->register_sg_base + 0x88;
	ch->reg_sg_credits = ch->register_sg_base + 0x8c;

	sc0710_dma_chains_alloc(ch, ch->buf_size);
	printk(KERN_INFO "%s channel %d allocated\n", dev->name, nr);
	sc0710_dma_channel_chains_link(ch);
	sc0710_dma_chains_dump(ch);

	if (ch->mediatype == CHTYPE_VIDEO) {
		ret = sc0710_video_register(ch);
	} else if (ch->mediatype == CHTYPE_AUDIO) {
		ret = sc0710_audio_register(dev);
	}

	return ret;
}

int sc0710_dma_channel_resize(struct sc0710_dev *dev, u32 nr, enum sc0710_channel_dir_e direction,
	u32 baseaddr,
	enum sc0710_channel_type_e mediatype)
{
	struct sc0710_dma_channel *ch = &dev->channel[nr];

	if (nr >= SC0710_MAX_CHANNELS)
		return -1;
	if (!dev->fmt)
		return -1;

	sc0710_dma_chains_free(ch);

	printk(KERN_INFO "%s channel %d resized for framesize %d\n", dev->name, nr, dev->fmt->framesize);

	if (ch->mediatype == CHTYPE_VIDEO) {
		ch->numDescriptorChains = DMA_TRANSFER_CHAINS;
		ch->buf_size = dev->fmt->framesize;
		printk("Resizing channel for size %d\n", ch->buf_size);
	} else if (ch->mediatype == CHTYPE_AUDIO) {
		ch->numDescriptorChains = DMA_TRANSFER_CHAINS;
		ch->buf_size = DMA_AUDIO_TRANSFER_SIZE;
	} else {
		ch->numDescriptorChains = 0;
	}

	ch->pt_size = PAGE_SIZE * 2;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 18, 0)
	ch->pt_cpu = dma_alloc_coherent(&((struct pci_dev *)dev->pci)->dev, ch->pt_size, &ch->pt_dma, GFP_ATOMIC);
#else
	ch->pt_cpu = pci_alloc_consistent(dev->pci, ch->pt_size, &ch->pt_dma);
#endif
	if (ch->pt_cpu == 0)
		return -1;

	memset(ch->pt_cpu, 0, ch->pt_size);
	sc0710_dma_chains_alloc(ch, ch->buf_size);
	printk(KERN_INFO "%s channel %d allocated\n", dev->name, nr);
	sc0710_dma_channel_chains_link(ch);
	sc0710_dma_chains_dump(ch);

	return 0;
}

void sc0710_dma_channel_free(struct sc0710_dev *dev, u32 nr)
{
	struct sc0710_dma_channel *ch = &dev->channel[nr];

	if (nr >= SC0710_MAX_CHANNELS)
		return;
	if (ch->enabled == 0)
		return;

	ch->enabled = 0;

	if (ch->mediatype == CHTYPE_VIDEO) {
		sc0710_video_unregister(ch);
	} else if (ch->mediatype == CHTYPE_AUDIO) {
		sc0710_audio_unregister(dev);
	}

	sc0710_dma_chains_free(ch);
	printk(KERN_INFO "%s channel %d deallocated\n", dev->name, nr);
}

int sc0710_dma_channel_start_prep(struct sc0710_dma_channel *ch)
{
	sc_write(ch->dev, 1, ch->reg_dma_control_w1c, 0x00000001);

	ch->dma_completed_descriptor_count_last = 0;
	sc_write(ch->dev, 1, ch->reg_dma_completed_descriptor_count, 1);
	sc_write(ch->dev, 1, ch->reg_sg_start_h, ch->pt_dma >> 32);
	sc_write(ch->dev, 1, ch->reg_sg_start_l, ch->pt_dma);
	sc_write(ch->dev, 1, ch->reg_sg_adj, 0);

	return 0;
}

int sc0710_dma_channel_stop(struct sc0710_dma_channel *ch)
{
	sc_write(ch->dev, 1, ch->reg_dma_control_w1c, 0x00000001);
	sc0710_things_per_second_reset(&ch->bitsPerSecond);
	sc0710_things_per_second_reset(&ch->descPerSecond);
	ch->state = STATE_STOPPED;
	return 0;
}

int sc0710_dma_channel_start(struct sc0710_dma_channel *ch)
{
	sc_write(ch->dev, 1, ch->reg_dma_control_w1s, 0x00000001);
	ch->state = STATE_RUNNING;
	return 0;
}

enum sc0710_channel_state_e sc0710_dma_channel_state(struct sc0710_dma_channel *ch)
{
	return ch->state;
}
