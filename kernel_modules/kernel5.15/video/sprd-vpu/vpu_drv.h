/*
*SPDX-FileCopyrightText: 2019 Unisoc (Shanghai) Technologies Co.Ltd
*SPDX-License-Identifier: GPL-2.0-only
*/

#include <linux/cdev.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/sched.h>
#include <linux/semaphore.h>
#include <linux/slab.h>
#include <linux/sprd_iommu.h>
#include <linux/sprd_ion.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/wait.h>
#include <linux/notifier.h>
#include <linux/compat.h>
#include "sprd_vpu.h"

struct clock_name_map_t {
	unsigned long freq;
	char *name;
	struct clk *clk_parent;
};

enum {
	VPU_DOMAIN_EB = 0,
	RESET
};

struct register_gprrr {
	struct regmap *gpr;
	uint32_t reg;
	uint32_t mask;
};

static char *tb_name[] = {
	"vsp-domain-eb-syscon",
	"reset-syscon"
};

static char *vpu_clk_src[] = {
	"clk_src_76m8",
	"clk_src_128m",
	"clk_src_256m",
	"clk_src_307m2",
	"clk_src_384m",
	"clk_src_512m",
	"clk_src_614m4",
	"clk_src_680m"
};

typedef struct mmu_reg
{
	u32 mmu_vaor_addr_rd;
	u32 mmu_vaor_addr_wr;
	u32 mmu_inv_addr_rd;
	u32 mmu_inv_addr_wr;
	u32 mmu_uns_addr_rd;
	u32 mmu_uns_addr_wr;
	u32 mmu_vpn_paor_rd;
	u32 mmu_vpn_paor_wr;
	u32 mmu_ppn_paor_rd;
	u32 mmu_ppn_paor_wr;
	u32 mmu_int_en_off;
	u32 mmu_int_clr_off;
	u32 mmu_int_sts_off;
	u32 mmu_int_raw_off;
	u32 mmu_int_msk_off;
	u32 mmu_en_off;
	u32 mmu_update_off;
}MMU_REG;

struct vpu_clk {
	unsigned int freq_div;

	struct clk *core_clk;
	struct clk *core_parent_clk;
	struct clk *clk_domain_eb;
	struct clk *clk_dev_eb;
	struct clk *clk_ckg_eb;
	struct clk *clk_ahb_vsp;
	struct clk *ahb_parent_clk;
	struct clk *clk_ahb_gate_vsp_eb;
	struct clk *clk_mm_eb;
	struct clk *clk_axi_gate_vsp;
	struct clk *clk_vsp_mq_ahb_eb;
	struct clk *clk_emc_vsp;
	struct clk *clk_vsp_ahb_mmu_eb;
	struct clk *emc_parent_clk;
};

struct vpu_qos_cfg {
	u8 awqos;
	u8 arqos_high;
	u8 arqos_low;
	unsigned int reg_offset;
};

struct vpu_platform_data {
	struct platform_device *pdev;
	struct device *dev;
	struct miscdevice mdev;
	const struct core_data *p_data;
	struct vpu_qos_cfg qos;
	struct vpu_clk clk;
	struct register_gprrr regs[ARRAY_SIZE(tb_name)];
	struct clock_name_map_t clock_name_map[ARRAY_SIZE(vpu_clk_src)];
	struct semaphore vpu_mutex;
	struct wakeup_source *vpu_wakelock;
	bool qos_exist_flag;

	void __iomem *vpu_base;
	void __iomem *glb_reg_base;

	unsigned long phys_addr;
	unsigned int version;
	unsigned int max_freq_level;
	unsigned int qos_reg_offset;

	int irq;
	int condition_work;
	int vpu_int_status;

	bool iommu_exist_flag;
	bool is_clock_enabled;

	struct mutex map_lock;
	struct list_head map_list;

	wait_queue_head_t wait_queue_work;
	atomic_t instance_cnt;
	struct vpu_fp *inst_ptr;
};

struct vpu_ops {
	int (*get_eb_clk)(struct vpu_platform_data *data, struct device_node *np);
	int (*clock_enable)(struct vpu_platform_data *data);
	void (*clock_disable)(struct vpu_platform_data *data);
	void (*check_pw_status)(struct vpu_platform_data *data);
	u32 (*get_reset_mask)(struct vpu_platform_data *data);
};

extern const struct vpu_ops vpu_r1p0;
extern const struct vpu_ops vsp_hevc_lite;
extern const struct vpu_ops vsp_sharkl3;
extern const struct vpu_ops vsp_pike2;

struct core_data {
	const char *name;
	irqreturn_t (*isr)(int irq, void *data);
	bool is_enc;
	u32 dev_eb_mask;
	const struct vpu_ops *ops;
	struct mmu_reg *mmu_reg;
};

struct vpu_fp {
	struct vpu_platform_data *dev_data;
	bool is_vpu_acquired;
	bool is_clock_enabled;
	bool is_wakelock_got;
};

#define ARM_INT_STS_OFF		0x10
#define ARM_INT_MASK_OFF	0x14
#define ARM_INT_CLR_OFF		0x18
#define ARM_INT_RAW_OFF		0x1c

#define VPU_INT_STS_OFF		0x0
#define VPU_INT_MASK_OFF	0x04
#define VPU_INT_CLR_OFF		0x08
#define VPU_INT_RAW_OFF		0x0c
#define VPU_AXI_PAUSE_OFF	0x18
#define VPU_AXI_STS_OFF		0x1c

#define VPU_AQUIRE_TIMEOUT_MS	500
#define VPU_INIT_TIMEOUT_MS	200
/*vpu dec*/
#define DEC_BSM_OVF_ERR		BIT(0)
#define DEC_VLD_ERR		BIT(4)
#define DEC_TIMEOUT_ERR		BIT(5)
#define DEC_MMU_INT_ERR		BIT(13)
#define DEC_AFBCD_HERR		BIT(14)
#define DEC_AFBCD_PERR		BIT(15)

/*vpu enc*/
#define ENC_BSM_OVF_ERR		BIT(2)
#define ENC_TIMEOUT_ERR		BIT(3)
#define ENC_AFBCD_HERR		BIT(4)
#define ENC_AFBCD_PERR		BIT(5)
#define ENC_MMU_INT_ERR		BIT(6)

#define MMU_RD_WR_ERR		0xff

irqreturn_t enc_core0_isr(int irq, void *data);
irqreturn_t enc_core1_isr(int irq, void *data);
irqreturn_t common_isr(int irq, void *data);
void vpu_qos_config(struct vpu_platform_data *data);
void get_freq_clk(struct vpu_platform_data *data, struct device_node *np);
int get_eb_clk_lite(struct vpu_platform_data *data, struct device_node *np);
int clock_enable_lite(struct vpu_platform_data *data);
void clock_disable_lite(struct vpu_platform_data *data);
void clr_vpu_interrupt_mask(struct vpu_platform_data *data);
u32 get_reset_mask(struct vpu_platform_data *data);
struct clk *get_clk_src_name(struct clock_name_map_t clock_name_map[], unsigned int freq_level, unsigned int max_freq_level);
void vsp_check_pw_status(struct vpu_platform_data *data);
int find_freq_level(struct clock_name_map_t clock_name_map[], unsigned long freq, unsigned int max_freq_level);
int get_iova(void *inst_ptr, struct vpu_platform_data *data, struct iommu_map_data *mapdata, void __user *arg);
int free_iova(void *inst_ptr, struct vpu_platform_data *data, struct iommu_map_data *ummapdata);
long compat_vpu_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
int vsp_get_dmabuf(int fd, struct dma_buf **dmabuf, void **buf, size_t *size);
void non_free_bufs_check(void *inst_ptr, struct vpu_platform_data *data);

