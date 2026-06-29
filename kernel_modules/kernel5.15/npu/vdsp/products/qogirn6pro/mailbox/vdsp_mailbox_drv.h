/**
 * SPDX-FileContributor: vdsp driver mailbox to implement communication between ap and vdsp.
 */

#ifndef VDSP_MAILBOX_DRV_H
#define VDSP_MAILBOX_DRV_H

#include <linux/of_address.h>
#include <linux/interrupt.h>
typedef irqreturn_t (*mbox_handle)(void *ptr, void *private);

enum xrp_irq_mode {
	XRP_IRQ_NONE,
	XRP_IRQ_LEVEL,
	XRP_IRQ_EDGE,
	XRP_IRQ_EDGE_SW,
	XRP_IRQ_MAX,
};

struct mbox_dts_cfg_tag {
	struct regmap *gpr;
	u32 enable_reg;
	u32 mask_bit;
	struct resource inboxres;
	struct resource outboxres;
	u32 inbox_irq;
	u32 outbox_irq;
	u32 core_cnt;
	u32 version;
};

struct mbox_chn_tag {
	mbox_handle mbox_smsg_handler;
	void *mbox_priv_data;
};

struct mbox_operations_tag {
	int (*cfg_init) (struct mbox_dts_cfg_tag *, u8 *);
	int (*phy_register_irq_handle) (u8, mbox_handle, void *);
	int (*phy_unregister_irq_handle) (u8);
	irqreturn_t(*src_irqhandle) (int, void *);//no use
	irqreturn_t(*recv_irqhandle) (int, void *);
	int (*phy_send) (u8, u64);
	void (*process_bak_msg) (void);
	int (*enable) (void *ctx);
	int (*disable) (void *ctx);
};

struct vdsp_mbox_ops;
struct vdsp_mbox_ctx_desc {
	struct regmap *mbox_en_cfg;
	struct mbox_dts_cfg_tag mbox_cfg;
	/* how IRQ is used to notify the device of incoming data */
	enum xrp_irq_mode irq_mode;
	struct vdsp_mbox_ops *ops;
	spinlock_t mbox_spinlock;
	uint32_t mbox_active;
};

struct vdsp_mbox_ops {
	int (*ctx_init) (struct vdsp_mbox_ctx_desc *ctx);
	int (*ctx_deinit) (struct vdsp_mbox_ctx_desc *ctx);
	irqreturn_t(*irq_handler) (int irq, void *arg);
	int (*irq_register) (u8 idx, mbox_handle handler, void *arg);
	int (*irq_unregister) (u8 idx);
	int (*irq_send) (u8 idx, u64 msg);
	int (*irq_clear) (int idx);
	int (*mbox_init) (void);
};

struct vdsp_mbox_ctx_desc *get_vdsp_mbox_ctx_desc(void);
#endif /* SPRD_MAILBOX_DRV_H */
