// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2020 Unisoc Communications Inc.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */
#define pr_fmt(fmt) "sprd-minidump: " fmt
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#define FW_RING_R			0
#define FW_RING_W			1
#define RX_RING_SIZE		(1024*1024)
#define SUCCESS			    0
#define ERR_RING_FULL		1
#define ERR_MALLOC_FAIL	    2
#define ERR_BAD_PARAM		3
#define FALSE				false
#define TRUE				true
#define FW_RING_REMAIN(rp, wp, size) \
	((u_long)(wp) >= (u_long)(rp) ? \
	((size)-(u_long)(wp)+(u_long)(rp)) : \
	((u_long)(rp)-(u_long)(wp)))
struct dump_ring_t {
	unsigned long size;
	char *pbuff;
	char *rp;
	char *wp;
	char *end;
	bool reset_rp;
	struct mutex *plock;
	int (*memcpy_rd)(char *dest, char *src, size_t cnt);
	int (*memcpy_wr)(char *dest, char *src, size_t cnt);
};
struct dump_device {
	struct dump_ring_t	*ring_dev;
	struct miscdevice	wcn_fw_dump_device;
	wait_queue_head_t	rxwait;
};
static struct dump_ring_t *fw_rx_ring;
static struct dump_device *dump_dev;
static unsigned long fw_ring_remain(struct dump_ring_t *pring)
{
	return (unsigned long)FW_RING_REMAIN(pring->rp,
						   pring->wp, pring->size);
}
unsigned long fw_ring_free_space(void)
{
	return fw_ring_remain(dump_dev->ring_dev);
}
static unsigned long fw_ring_content_len(struct dump_ring_t *pring)
{
	return pring->size - fw_ring_remain(pring);
}
static char *fw_ring_start(struct dump_ring_t *pring)
{
	return pring->pbuff;
}
static char *fw_ring_end(struct dump_ring_t *pring)
{
	return pring->end;
}
void fw_ring_reset(void)
{
	dump_dev->ring_dev->wp = dump_dev->ring_dev->pbuff;
	dump_dev->ring_dev->rp = dump_dev->ring_dev->wp;
}
static bool fw_ring_over_loop(struct dump_ring_t *pring,
				u_long len,
				int rw)
{
	if (rw == FW_RING_R)
		return (u_long)pring->rp + len > (u_long)fw_ring_end(pring);
	else
		return (u_long)pring->wp + len > (u_long)fw_ring_end(pring);
}
static void fw_ring_destroy(struct dump_ring_t *pring)
{
	if (pring) {
		if (pring->pbuff) {
			pr_debug(" %s free pbuff\n", __func__);
			vfree(pring->pbuff);
			pring->pbuff = NULL;
		}
		if (pring->plock) {
			pr_debug("%s free plock\n", __func__);
			mutex_destroy(pring->plock);
			vfree(pring->plock);
			pring->plock = NULL;
		}
		pr_debug("%s free pring\n", __func__);
		vfree(pring);
		pring = NULL;
	}
}
static struct dump_ring_t *fw_ring_init(unsigned long size,
					  int (*rd)(char*, char*, size_t),
					  int (*wr)(char*, char*, size_t))
{
	struct dump_ring_t *pring = NULL;

	if (!rd || !wr) {
		pr_err("Ring must assign callback\n");
		return NULL;
	}
	do {
		pring = vmalloc(sizeof(struct dump_ring_t));
		if (!pring)
			break;
		pring->pbuff = vmalloc(size);
		if (!pring->pbuff)
			break;
		pring->plock = vmalloc(sizeof(struct mutex));
		if (!pring->plock)
			break;
		mutex_init(pring->plock);
		memset(pring->pbuff, 0, size);
		pring->size = size;
		pring->rp = pring->pbuff;
		pring->wp = pring->pbuff;
		pring->end = (char *)((u_long)pring->pbuff + (pring->size - 1));
		pring->reset_rp = false;
		pring->memcpy_rd = rd;
		pring->memcpy_wr = wr;
		return pring;
	} while (0);
	fw_ring_destroy(pring);
	return NULL;
}
static int fw_ring_read(struct dump_ring_t *pring, char *buf, int len)
{
	int len1, len2 = 0;
	int cont_len = 0;
	int read_len = 0;
	char *pstart = NULL;
	char *pend = NULL;

	if (!buf || !pring || !len) {
		pr_err("%s: Param Error!,buf=%p,pring=%p,len=%d\n",
		       __func__, buf, pring, len);
		return -ERR_BAD_PARAM;
	}
	mutex_lock(pring->plock);
	pr_debug("reset_rp[%d] rp[%p] wp[%p]\n",
		 pring->reset_rp, pring->rp, pring->wp);
	if (pring->reset_rp == true) {
		pring->rp = pring->wp;
		pring->reset_rp = false;
	}
	cont_len = fw_ring_content_len(pring);
	read_len = cont_len >= len ? len : cont_len;
	pstart = fw_ring_start(pring);
	pend = fw_ring_end(pring);
	pr_debug("len=%d, buf=%p, start=%p, end=%p, rp=%p\n",
		 read_len, buf, pstart, pend, pring->rp);
	if ((read_len == 0) || (cont_len == 0)) {
		pr_debug("read_len=0 or Ring empty\n");
		mutex_unlock(pring->plock);
		return 0;
	}
	if (fw_ring_over_loop(pring, read_len, FW_RING_R)) {
		pr_debug("Ring loopover\n");
		len1 = pend - pring->rp + 1;
		len2 = read_len - len1;
		pring->memcpy_rd(buf, pring->rp, len1);
		pring->memcpy_rd((buf + len1), pstart, len2);
		pring->rp = (char *)((u_long)pstart + len2);
	} else {
		pring->memcpy_rd(buf, pring->rp, read_len);
		pring->rp += read_len;
	}
	pr_debug("Ring did read len[%d]\n", read_len);
	mutex_unlock(pring->plock);
	return read_len;
}
static int fw_ring_write(struct dump_ring_t *pring, char *buf, int len)
{
	int len1, len2 = 0, cnt = 5;
	char *pstart = NULL;
	char *pend = NULL;
	bool check_rp = false;

	if (!pring || !buf || !len) {
		pr_err("%s: Param Error!,buf=%p,pring=%p,len=%d\n",
		       __func__, buf, pring, len);
		return -ERR_BAD_PARAM;
	}
	pstart = fw_ring_start(pring);
	pend = fw_ring_end(pring);
	pr_debug("start=%p, end=%p, buf=%p, len=%d, wp=%p, rst_rp[%d]\n",
		 pstart, pend, buf, len, pring->wp, pring->reset_rp);
	while (cnt > 0 && (fw_ring_free_space() - 1) < (unsigned long)len) {
		pr_info("%s: minidump remaining ringbuff not enough\n", __func__);
		msleep(500);
		if (--cnt == 0) {
			pr_err("ringbuf is full, timeout: 2.5s\n");
			return -ERR_RING_FULL;
		}
	}
	if (fw_ring_over_loop(pring, len, FW_RING_W)) {
		pr_info("Ring overloop\n");
		len1 = pend - pring->wp + 1;
		len2 = len - len1;
		pring->memcpy_wr(pring->wp, buf, len1);
		pring->memcpy_wr(pstart, (buf + len1), len2);
		if (pring->wp < pring->rp)
			pring->reset_rp = true;
		else
			check_rp = true;
		pring->wp = (char *)((u_long)pstart + len2);
	} else {
		pring->memcpy_wr(pring->wp, buf, len);
		if (pring->wp < pring->rp)
			check_rp = true;
		pring->wp += len;
	}
	if (check_rp && pring->wp > pring->rp)
		pring->reset_rp = true;
	pr_debug("Ring Wrote len[%d]\n", len);
	return len;
}
int fw_dump_write(void *buf, int len)
{
	ssize_t count = 0;

	count = fw_ring_write(dump_dev->ring_dev, (char *)buf, len);
	if (count > 0)
		wake_up_interruptible(&dump_dev->rxwait);
	return count;
}
static int fw_memcpy_rd(char *dest, char *src, size_t count)
{
	return copy_to_user(dest, src, count);
}
static int fw_memcpy_wr(char *dest, char *src, size_t count)
{
	memcpy(dest, src, count);
	return 0;
}
static int fw_device_init(void)
{
	dump_dev = kzalloc(sizeof(*dump_dev), GFP_KERNEL);
	if (!dump_dev)
		return -ENOMEM;
	dump_dev->ring_dev = fw_rx_ring;
	init_waitqueue_head(&dump_dev->rxwait);
	return 0;
}
static int fw_device_destroy(void)
{
	kfree(dump_dev);
	dump_dev = NULL;
	return 0;
}
static int wcn_mini_dump_open(struct inode *inode, struct file *filp)
{
	int minor = iminor(filp->f_path.dentry->d_inode);
	int minor1 = iminor(inode);
	int major = imajor(inode);

	pr_info("minidump open. minor=%d,minor1=%d,major=%d\n", minor, minor1, major);
	return 0;
}
static int wcn_mini_dump_release(struct inode *inode, struct file *filp)
{
	pr_info("minidump release.");
	return 0;
}
static ssize_t wcn_mini_dump_read(struct file *filp,
				char __user *buf,
				size_t count,
				loff_t *pos)
{
	ssize_t len = 0;

	len = fw_ring_read(fw_rx_ring, (char *)buf, count);
	return len;
}
static unsigned int wcn_mini_dump_poll(struct file *filp, poll_table *wait)
{
	unsigned int mask = 0;

	poll_wait(filp, &dump_dev->rxwait, wait);
	if (fw_ring_content_len(fw_rx_ring) > 0)
		mask |= POLLIN | POLLRDNORM;
	return mask;
}
static const struct file_operations wcn_fw_dump_fops = {
	.owner = THIS_MODULE,
	.read = wcn_mini_dump_read,
	.open = wcn_mini_dump_open,
	.release = wcn_mini_dump_release,
	.poll = wcn_mini_dump_poll,
};

static struct miscdevice wcn_fw_dump_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "wcn_mini_dump",
	.fops = &wcn_fw_dump_fops,
};

/*
 * DEVFS interfaces:
 * /dev/wcn_mini_dump [debug]
 */
int wcn_fw_dump_init(void)
{
	int ret;

	fw_rx_ring = fw_ring_init(RX_RING_SIZE,
				      fw_memcpy_rd, fw_memcpy_wr);
	if (!fw_rx_ring) {
		pr_err("Ring malloc error\n");
		return -ERR_MALLOC_FAIL;
	}
	do {
		ret = fw_device_init();
		if (ret != 0) {
			fw_ring_destroy(fw_rx_ring);
			break;
		}
		ret = misc_register(&wcn_fw_dump_device);
		if (ret != 0) {
			fw_ring_destroy(fw_rx_ring);
			fw_device_destroy();
			break;
		}
		dump_dev->wcn_fw_dump_device = wcn_fw_dump_device;
	} while (0);
	if (ret != 0)
		pr_err("misc register error\n");
	return ret;
}
void wcn_fw_dump_exit(void)
{
	fw_ring_destroy(fw_rx_ring);
	fw_device_destroy();
	misc_deregister(&wcn_fw_dump_device);
}
