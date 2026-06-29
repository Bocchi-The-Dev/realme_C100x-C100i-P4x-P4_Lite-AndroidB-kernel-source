/**
 * SPDX-FileContributor: vdsp driver hardware function: power and contrl.
 */

#ifndef _VDSP_HW_H
#define _VDSP_HW_H

#include <linux/irqreturn.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include "vdsp_mailbox_drv.h"
#include "vdsp_reg.h"

#define VDSP_FIRMWIRE_SIZE (1024*1024*6)
#define VDSP_DRAM_ADDR     (0x34000000)
#define VDSP_DRAM_SIZE     (256*1024)

#define DRIVER_NAME        "vdsp"

/*CAMSYS AHB 0x30000000*/
#define MM_SYS_EN		(0x0)
#define VDSP_BLK_EN		(0x8)
/*
[31:13]	Reserved		RO	19'h0
[12]	Ivau_en		RW	1'b0	Set/Clear		not in use
[11]	Dvau_en		RW	1'b0	Set/Clear		VDSP MSTD VAU enable: 1'b0 disable; 1'b1, enable
[10]	iDvau_en		RW	1'b0	Set/Clear		VDSP iDMA VAU enable: 1'b0 disable; 1'b1, enable
[9]	vdsp_mst_busmon_en	RW	1'b0	set/clear	VDSP axi master bus monitor enable: 1'b0 disable; 1'b1, enable
[8]	vdsp_slv_busmon_en	RW	1'b0	set/clear	VDSP slave config bus monitor enable: 1'b0 disable; 1'b1, enable
[7]	vdsp_blk_en	RW	1'b0	set/clear		not in use
[6]	uart_en		RW	1'h0	set/clear		uart enalbe: 1'b0 disable; 1'b1, enable
[5]	vdsp_blk_cfg_en	RW	1'h0	set/clear		not in use
[4]	vdsp_mtx_data	RW	1'h0	set/clear		not in use
[3]	vdsp_tck_en	RW	1'h0	set/clear		vdsp djtag tck enalbe: 1'b0 disable; 1'b1, enable
[2]	vdma_en		RW	1'h0	set/clear		vdma enalbe: 1'b0 disable; 1'b1, enable
[1]	vdsp_m_en	RW	1'h0	set/clear		vdsp_m (VDSP bus) clock enable:  1'b0 disable; 1'b1, enable
[0]	vdsp_en		RW	1'h0	set/clear
*/
#define VDSP_INT_MASK		(0x18)
/*
[5]	vdsp_mailbox_int_mask	RW	1'h0	set/clear
[4]	vdsp_idma_vau_int_mask	RW	1'h0	set/clear
[3]	vdsp_mstd_vau_int_mask	RW	1'h0	set/clear
[2]	vdsp_uart_int_mask	RW	1'h0	set/clear
[1]	vdsp_vdma_int_mask	RW	1'h0	set/clear
[0]	vdsp_vdma_vau_int_mask	RW	1'h0	set/clear
*/
#define VDSP_CORE_CFG		(0xBC)
/*
[31:16]	PRID		RW	16'h0	VDSP processor ID: the value is latched at reset
[15:13]	Reserved		RO	3'h0	Reserved
[12]	vdsp_pwaitmode	RO	1'b0	indicate VDSP core is in sleep mode: 1'b0, normal mode; 1'b1, sleep mode
[11]	DBGEN		RW	1'b0	Non-secure, invasive debug enable: 1'b0, disable; 1'b1, enable
[10]	NIDEN		RW	1'b0	Non-secure, non-invasive debug enable: 1'b0, disable; 1'b1, enable
[9]	SPIDEN		RW	1'b0	Decure, invasive debug enable: 1'b0, disable; 1'b1, enable
[8]	SPNIDEN		RW	1'b0	Secure, non-invasive debug enable: 1'b0, disable; 1'b1, enable
[7:6]	vdsp_trigout_idma	RO	2'd0	"Indicates the completion of an iDMA descriptor that is configured to sendtrigger out upon finish."
[5:4]	vdsp_trigin_idma	RW	2'h0	The trigger input that increases the iDMA trigger count when it is asserted.
[3]	stat_vector_sel	RW	1'b0	boot address select, latched at reset: 1'b0: 0x8000_0000; 1'b1: 0x4000_0000
[2]	runstallonreset	RW	1'b1	run or stall the VDSP: 1'b0, run; 1'b1, stall
[1:0]	dcache_dram_flush	RW	2'd0	not in use
*/
#define VDSP_PWAITMODE    	(1U << 12) // BIT(12)
#define VDSP_RUNSTALL     	(1U << 2) // BIT(2)

#define VDSP_FATAL_INFO_LOW	(0xc0) //not support set/clr
#define VDSP_FATAL_INFO_HIGH	(0xc4) //not support set/clr
#define REG_RESET		(0xd4) //set/clr, (VDSP_BLK_SOFTRST)
/*
[5]	vdsp_soft_rst		RW	1'b0	set/clear
[4]	vdsp_debug_soft_rst	RW	1'b1	set/clear
[3]	vdsp_idma_vau_soft_rst	RW	1'b0	set/clear
[2]	vdsp_mstd_vau_soft_rst	RW	1'b0	set/clear
[1]	vdma_soft_rst		RW	1'b0	set/clear
[0]	vdma_vau_soft_rst		RW	1'b0	set/clear
*/
#define VDSP_RESET        		(1U << 5) //BIT(5)

/*PMU REG (0x6491_0000): offset */
#define DSLP_ENA                           (0x1FC)  //set/clr (VDSP_BLK_DSLP_ENA)
#define CORE_INT_DISABLE            (0x25C)  //set/clr(VDSP_BLK_CORE_INT_DISABLE)
#define PD_CFG_0		(0x2FC)  //set/clr(PD_VDSP_BLK_CFG_0)
#define PD_STATUS                         (0x510)  //not support set/clr (PWR_STATUS_DBG_8)

enum {
	XRP_DSP_SYNC_IRQ_MODE_NONE = 0x0,
	XRP_DSP_SYNC_IRQ_MODE_LEVEL = 0x1,
	XRP_DSP_SYNC_IRQ_MODE_EDGE = 0x2,
};

enum vdsp_init_flags {
	/*! Use interrupts in DSP->host communication */
	XRP_INIT_USE_HOST_IRQ = 0x1,
};

struct xvp;

struct qos_info {
	uint8_t ar_qos_vdsp_msti;
	uint8_t ar_qos_vdsp_mstd;
	uint8_t aw_qos_vdsp_mstd;
	uint8_t ar_qos_vdsp_idma;
	uint8_t aw_qos_vdsp_idma;
	uint8_t ar_qos_vdma;
	uint8_t aw_qos_vdma;
	uint8_t ar_qos_threshold;
	uint8_t aw_qos_threshold;
};

struct vdsp_hw {
	struct xvp *xrp;

	struct regmap *mm_ahb;
	struct regmap *mailbox;
	struct qos_info qos;

	phys_addr_t mbox_phys;
	struct vdsp_mbox_ctx_desc *vdsp_mbox_desc;

	phys_addr_t vdsp_reserved_mem_addr;
	size_t vdsp_reserved_mem_size;

	/* how IRQ is used to notify the device of incoming data */
	enum xrp_irq_mode device_irq_mode;
	/*
	 * offset of device IRQ register in MMIO region (device side)
	 * bit number
	 * device IRQ#
	 */
	u32 device_irq[3];
	/* offset of devuce IRQ register in MMIO region (host side) */
	u32 device_irq_host_offset;
	/* how IRQ is used to notify the host of incoming data */
	enum xrp_irq_mode host_irq_mode;
	/*
	 * offset of IRQ register (device side)
	 * bit number
	 */
	u32 host_irq[2];

	s32 client_irq;
};

struct vdsp_side_sync_data {
	__u32 device_mmio_base;
	__u32 host_irq_mode;
	__u32 host_irq_offset;
	__u32 host_irq_bit;
	__u32 device_irq_mode;
	__u32 device_irq_offset;
	__u32 device_irq_bit;
	__u32 device_irq;

	__u32 vdsp_smsg_addr;
	__u32 vdsp_log_addr;
};

/*!
 * Hardware-specific operation entry points.
 * Hardware-specific driver passes a pointer to this structure to xrp_init
 * at initialization time.
 */
struct xrp_hw_ops {
	/*!
	 * Enable power/clock, but keep the core stalled.
	 * \param hw_arg: opaque parameter passed to xrp_init at initialization
	 *                time
	 */
	int (*enable) (void *hw_arg);
	/*!
	 * Diable power/clock.
	 *
	 * \param hw_arg: opaque parameter passed to xrp_init at initialization
	 *                time
	 */
	int (*disable) (void *hw_arg);
	/*!
	 * Reset the core.
	 *
	 * \param hw_arg: opaque parameter passed to xrp_init at initialization
	 *                time
	 */
	void (*reset) (void *hw_arg);
	/*!
	 * Unstall the core.
	 *
	 * \param hw_arg: opaque parameter passed to xrp_init at initialization
	 *                time
	 */
	void (*release) (void *hw_arg);
	/*!
	 * Stall the core.
	 *
	 * \param hw_arg: opaque parameter passed to xrp_init at initialization
	 *                time
	 */
	void (*halt) (void *hw_arg);

	/*! Get HW-specific data to pass to the DSP on synchronization
	 *
	 * \param hw_arg: opaque parameter passed to xrp_init at initialization
	 *                time
	 * \param sz: return size of sync data here
	 * \return a buffer allocated with kmalloc that the caller will free
	 */
	void *(*get_hw_sync_data) (void *hw_arg, size_t *sz, uint32_t log_addr);

	/*!
	 * Send IRQ to the core.
	 *
	 * \param hw_arg: opaque parameter passed to xrp_init at initialization
	 *                time
	 */
	int (*send_irq) (void *hw_arg);

	/*!
	 * memcpy data/code to device-specific memory.
	 */
	void (*memcpy_tohw) (void __iomem *dst, const void *src, size_t sz);
	/*!
	 * memset device-specific memory.
	 */
	void (*memset_hw) (void __iomem *dst, int c, size_t sz);
	/*!
	 * Check DSP status.
	 *
	 * \param hw_arg: opaque parameter passed to xrp_init at initialization
	 *                time
	 * \return whether the core has crashed and needs to be restarted
	 */
	bool(*panic_check) (void *hw_arg);

	/*set qos */
	int (*set_qos) (void *hw_arg);

	/*request irq */
	int (*vdsp_request_irq) (void *xvp_arg, void *hw_arg);

	/*free irq */
	void (*vdsp_free_irq) (void *xvp_arg, void *hw_arg);

	int (*stop_vdsp) (void *hw_arg);

	 /*communication*/
	int (*init_communication_hw) (void *hw_arg);
	int (*deinit_communication_hw) (void *hw_arg);
};

long sprd_vdsp_init(struct platform_device *pdev, enum vdsp_init_flags flags,
	const struct xrp_hw_ops *hw, void *hw_arg);
int sprd_vdsp_deinit(struct platform_device *pdev);

/*!
 * Notify generic XRP driver of possible IRQ from the DSP.
 *
 * \param xvp: pointer to struct xvp returned from xrp_init* call
 * \return whether IRQ was recognized and handled
 */
irqreturn_t xrp_irq_handler(struct xvp *xvp);
irqreturn_t vdsp_log_irq_handler(void *private);

int vdsp_hw_irq_register(void *data);
int vdsp_hw_irq_unregister(void);

#endif
