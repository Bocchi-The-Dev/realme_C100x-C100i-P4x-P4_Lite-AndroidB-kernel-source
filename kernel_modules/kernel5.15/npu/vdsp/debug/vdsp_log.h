/**
 * SPDX-FileContributor: vdsp driver log funtion.
 */

#ifndef _VDSP_LOG_H_
#define _VDSP_LOG_H_

#include <linux/types.h>

#define VDSP_LINE_BUFFER_SIZE 256

struct xvp;

/*
 * Ring buffer that supports one secure producer thread and one
 * linux side consumer thread.
 */
struct log_rb {
	volatile uint32_t alloc;
	volatile uint32_t put;
	uint32_t sz;
	volatile char data[0];
} __packed;


#define LOG_OVERFLOW_MODE	0
#define LOG_WAIT_MODE		1

struct vdsp_log_work;

struct vdsp_log_state {
	/*
	 * This lock is here to ensure only one consumer will read
	 * from the log ring buffer at a time.
	 */
	spinlock_t lock;
	uint32_t get;

	void *log_vaddr;
	struct workqueue_struct *nop_wq;
	struct vdsp_log_work __percpu *nop_works;
	char line_buffer[VDSP_LINE_BUFFER_SIZE];
};

struct vdsp_log_work {
	struct vdsp_log_state *vls;
	struct work_struct work;
};

int vdsp_log_init(struct xvp *xvp);
int vdsp_log_deinit(struct xvp *xvp);
int vdsp_log_coredump(struct xvp *xvp);
int vdsp_log_buf_iommu_map(struct xvp *xvp);
int vdsp_log_buf_iommu_unmap(struct xvp *xvp);
#endif

