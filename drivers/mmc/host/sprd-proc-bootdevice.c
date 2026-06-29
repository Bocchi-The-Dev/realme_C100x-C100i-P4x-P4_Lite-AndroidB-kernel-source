/*
 * create in 2021/1/7.
 * create emmc node in  /proc/bootdevice
 */

#include <linux/mmc/sprd-proc-bootdevice.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/errno.h>

#define  EMMC_TYPE      0
#define  UFS_TYPE       1
static struct __bootdevice bootdevice;
void set_bootdevice_cid(u32 *cid)
{
	memcpy(bootdevice.raw_cid, cid, sizeof(bootdevice.raw_cid));
}
void set_bootdevice_csd(u32 *csd)
{
	memcpy(bootdevice.raw_csd, csd, sizeof(bootdevice.raw_csd));
}
void set_bootdevice_product_name(char *product_name)
{
	strlcpy(bootdevice.product_name,
		product_name,
		sizeof(bootdevice.product_name));
}
void set_bootdevice_manfid(unsigned int manfid)
{
	bootdevice.manfid = manfid;
}

void set_bootdevice_oemid(unsigned short oemid)
{
	bootdevice.oemid = oemid;
}

void set_bootdevice_fwrev(u8 *fwrev)
{
	memcpy(bootdevice.fwrev, fwrev, MMC_FIRMWARE_LEN);
}
void set_bootdevice_ife_time_est_typ(u8 life_time_est_typ_a, u8 life_time_est_typ_b)
{
	bootdevice.life_time_est_typ_a = life_time_est_typ_a;
	bootdevice.life_time_est_typ_b = life_time_est_typ_b;
}
void set_bootdevice_rev(u8 rev)
{
	bootdevice.rev = rev;
}
void set_bootdevice_pre_eol_info(u8 pre_eol_info)
{
	bootdevice.pre_eol_info = pre_eol_info;
}
void set_bootdevice_serial(unsigned int serial)
{
	bootdevice.serial = serial;
}
void set_bootdevice_enhanced_area_offset(unsigned long long enhanced_area_offset)
{
	bootdevice.enhanced_area_offset = enhanced_area_offset;
}
void set_bootdevice_enhanced_area_size(unsigned int enhanced_area_size)
{
	bootdevice.enhanced_area_size = enhanced_area_size;
}
void set_bootdevice_prv(unsigned char prv)
{
	bootdevice.prv = prv;
}
void set_bootdevice_hwrev(unsigned char hwrev)
{
	bootdevice.hwrev = hwrev;
}
void set_bootdevice_ocr(u32	ocr)
{
	bootdevice.ocr = ocr;
}
void set_bootdevice_erase_size(unsigned int erase_size)
{
	bootdevice.erase_size = erase_size;
}
void set_bootdevice_size(unsigned int size)
{
	bootdevice.size = size;
}
void set_bootdevice_type(void)
{
	if (1)
		bootdevice.type = EMMC_TYPE;
	else
		bootdevice.type = UFS_TYPE;
}
void set_bootdevice_name(const char *name)
{
	bootdevice.name = name;
}
void set_bootdevice_ext_csd(u8 *ext_csd)
{
	int i;
	for (i = 0; i < 512; i++)
		bootdevice.ext_csd[i] = ext_csd[i];
}

static int sprd_cid_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%08x%08x%08x%08x\n", bootdevice.raw_cid[0], bootdevice.raw_cid[1],
		bootdevice.raw_cid[2], bootdevice.raw_cid[3]);
	return 0;
}

static int sprd_cid_open(struct inode *inode, struct file *file)
{
	return single_open(file, sprd_cid_show, inode->i_private);
}

static const struct proc_ops cid_fops = {
	.proc_open = sprd_cid_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int sprd_csd_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%08x%08x%08x%08x\n", bootdevice.raw_csd[0], bootdevice.raw_csd[1],
		bootdevice.raw_csd[2], bootdevice.raw_csd[3]);
	return 0;
}

static int sprd_csd_open(struct inode *inode, struct file *file)
{
	return single_open(file, sprd_csd_show, inode->i_private);
}

static const struct proc_ops csd_fops = {
	.proc_open = sprd_csd_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int sprd_prod_name_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%s\n", bootdevice.product_name);
	return 0;
}

static int sprd_prod_name_open(struct inode *inode, struct file *file)
{
	return single_open(file, sprd_prod_name_show, inode->i_private);
}

static const struct proc_ops prod_name_fops = {
	.proc_open = sprd_prod_name_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int sprd_manfid_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%x\n", bootdevice.manfid);
	return 0;
}

static int sprd_manfid_open(struct inode *inode, struct file *file)
{
	return single_open(file, sprd_manfid_show, inode->i_private);
}

static const struct proc_ops manfid_fops = {
	.proc_open = sprd_manfid_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int sprd_oemid_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%08x\n", bootdevice.oemid);
	return 0;
}

static int sprd_oemid_open(struct inode *inode, struct file *file)
{
	return single_open(file, sprd_oemid_show, inode->i_private);
}

static const struct proc_ops oemid_fops = {
	.proc_open = sprd_oemid_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int sprd_fwrev_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%x%x%x%x%x%x%x%x\n", bootdevice.fwrev[0], bootdevice.fwrev[1],
		   bootdevice.fwrev[2], bootdevice.fwrev[3], bootdevice.fwrev[4],
		   bootdevice.fwrev[5], bootdevice.fwrev[6], bootdevice.fwrev[7]);
	return 0;
}

static int sprd_fwrev_open(struct inode *inode, struct file *file)
{
	return single_open(file, sprd_fwrev_show, inode->i_private);
}

static const struct proc_ops fwrev_fops = {
	.proc_open = sprd_fwrev_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};


static int sprd_life_time_est_typ_a_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%x\n", bootdevice.life_time_est_typ_a);
	return 0;
}
static int sprd_life_time_est_typ_a_open(struct inode *inode, struct file *file)
{
	return single_open(file, sprd_life_time_est_typ_a_show, inode->i_private);
}

static const struct proc_ops life_time_est_typ_a_fops = {
	.proc_open = sprd_life_time_est_typ_a_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};
static int sprd_life_time_est_typ_b_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%x\n", bootdevice.life_time_est_typ_b);
	return 0;
}

static int sprd_life_time_est_typ_b_open(struct inode *inode, struct file *file)
{
	return single_open(file, sprd_life_time_est_typ_b_show, inode->i_private);
}

static const struct proc_ops life_time_est_typ_b_fops = {
	.proc_open = sprd_life_time_est_typ_b_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};
static int sprd_rev_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%x\n", bootdevice.rev);
	return 0;
}

static int sprd_rev_open(struct inode *inode, struct file *file)
{
	return single_open(file, sprd_rev_show, inode->i_private);
}

static const struct proc_ops rev_fops = {
	.proc_open = sprd_rev_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};
static int sprd_pre_eol_info_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%x\n", bootdevice.pre_eol_info);
	return 0;
}

static int sprd_pre_eol_info_open(struct inode *inode, struct file *file)
{
	return single_open(file, sprd_pre_eol_info_show, inode->i_private);
}

static const struct proc_ops pre_eol_info_fops = {
	.proc_open = sprd_pre_eol_info_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int sprd_enhanced_area_size_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%x\n", bootdevice.enhanced_area_size);
	return 0;
}
static int sprd_enhanced_area_size_open(struct inode *inode, struct file *file)
{
	return single_open(file, sprd_enhanced_area_size_show, inode->i_private);
}

static const struct proc_ops enhanced_area_size_fops = {
	.proc_open = sprd_enhanced_area_size_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int sprd_enhanced_area_offset_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%llu\n", bootdevice.enhanced_area_offset);
	return 0;
}

static int sprd_enhanced_area_offset_open(struct inode *inode, struct file *file)
{
	return single_open(file, sprd_enhanced_area_offset_show, inode->i_private);
}

static const struct proc_ops enhanced_area_offset_fops = {
	.proc_open = sprd_enhanced_area_offset_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int sprd_serial_show(struct seq_file *m, void *v)
{
	seq_printf(m, "0x%08x\n", bootdevice.serial);
	return 0;
}
static int sprd_serial_open(struct inode *inode, struct file *file)
{
	return single_open(file, sprd_serial_show, inode->i_private);
}

static const struct proc_ops serial_fops = {
	.proc_open = sprd_serial_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int sprd_hwrev_show(struct seq_file *m, void *v)
{
	seq_printf(m, "0x%x\n", bootdevice.hwrev);
	return 0;
}
static int sprd_hwrev_open(struct inode *inode, struct file *file)
{
	return single_open(file, sprd_hwrev_show, inode->i_private);
}
static const struct proc_ops hwrev_fops = {
	.proc_open = sprd_hwrev_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int sprd_prv_show(struct seq_file *m, void *v)
{
	seq_printf(m, "0x%x\n", bootdevice.prv);
	return 0;
}
static int sprd_prv_open(struct inode *inode, struct file *file)
{
	return single_open(file, sprd_prv_show, inode->i_private);
}
static const struct proc_ops prv_fops = {
	.proc_open = sprd_prv_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int sprd_ocr_show(struct seq_file *m, void *v)
{
	seq_printf(m, "0x%08x\n", bootdevice.ocr);
	return 0;
}
static int sprd_ocr_open(struct inode *inode, struct file *file)
{
	return single_open(file, sprd_ocr_show, inode->i_private);
}
static const struct proc_ops ocr_fops = {
	.proc_open = sprd_ocr_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};
static int sprd_erase_size_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%u\n", bootdevice.erase_size);
	return 0;
}
static int sprd_erase_size_open(struct inode *inode, struct file *file)
{
	return single_open(file, sprd_erase_size_show, inode->i_private);
}
static const struct proc_ops erase_size_fops = {
	.proc_open = sprd_erase_size_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};
static int sprd_type_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", bootdevice.type);
	return 0;
}
static int sprd_type_open(struct inode *inode, struct file *file)
{
	return single_open(file, sprd_type_show, inode->i_private);
}
static const struct proc_ops type_fops = {
	.proc_open = sprd_type_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};
static int sprd_size_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", bootdevice.size);
	return 0;
}
static int sprd_size_open(struct inode *inode, struct file *file)
{
	return single_open(file, sprd_size_show, inode->i_private);
}
static const struct proc_ops size_fops = {
	.proc_open = sprd_size_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int sprd_name_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%s\n", bootdevice.name);
	return 0;
}
static int sprd_name_open(struct inode *inode, struct file *file)
{
	return single_open(file, sprd_name_show, inode->i_private);
}
static const struct proc_ops name_fops = {
	.proc_open = sprd_name_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};


static int sprd_ext_csd_show(struct seq_file *m, void *v)
{
	int i;
	for (i = 0; i < 512; i++)
		seq_printf(m, "%02x", bootdevice.ext_csd[i]);
	return 0;
}
static int sprd_ext_csd_open(struct inode *inode, struct file *file)
{
	return single_open(file, sprd_ext_csd_show, inode->i_private);
}
static const struct proc_ops ext_csd_fops = {
	.proc_open = sprd_ext_csd_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};


#define PROC_MODE 0440
static const struct proc_ops *proc_fops_list[] = {
	&cid_fops,
	&csd_fops,
	&prod_name_fops,
	&manfid_fops,
	&oemid_fops,
	&fwrev_fops,
	&life_time_est_typ_a_fops,
	&life_time_est_typ_b_fops,
	&rev_fops,
	&pre_eol_info_fops,
	&enhanced_area_size_fops,
	&enhanced_area_offset_fops,
	&serial_fops,
	&hwrev_fops,
	&prv_fops,
	&ocr_fops,
	&erase_size_fops,
	&size_fops,
	&type_fops,
	&name_fops,
	&ext_csd_fops
};
static char *const sprd_emmc_node_info[] = {
	"cid",
	"csd",
	"product_name",
	"manfid",
	"oemid",
	"fw_version",
	"life_time_est_typ_a",
	"life_time_est_typ_b",
	"rev",
	"pre_eol_info",
	"enhanced_area_size",
	"enhanced_area_offset",
	"serial",
	"hwrev",
	"prv",
	"ocr",
	"erase_size",
	"size",
	"type",
	"name",
	"ext_csd"
};

static char *get_project_name(void)
{
    struct device_node *cmdline_node;
    const char *cmd_line = 0;
    char *temp_name = 0;
    int ret = 0;

    cmdline_node = of_find_node_by_path("/chosen");
    ret = of_property_read_string(cmdline_node,"bootargs",&cmd_line);
    if(!ret){
        temp_name = strstr(cmd_line,"prj_name=");
        if(temp_name != NULL){
        temp_name += strlen("prj_name=");
        pr_info("prj_name=%s\n",temp_name);
        }else{
            pr_err("read prj_name err");
        }
    }
    return temp_name;
}

static int isCruiserB4_prjname()
{
	char *prj_name = get_project_name();

	if (prj_name != NULL &&
		(strncmp(prj_name, "25391", 5) == 0 || strncmp(prj_name, "25392", 5) == 0 || strncmp(prj_name, "25393", 5) == 0 || \
		 strncmp(prj_name, "25394", 5) == 0 )) {
		printk("hardware_info is cruiserB4 prj.\n");
		return 1;
	}
	return 0;
}

int sprd_create_bootdevice_init(void)
{
	struct proc_dir_entry *bootdevice_dir;
	struct proc_dir_entry *prEntry;
	int i, node;

	if(isCruiserB4_prjname()){
        pr_err("%s: CruiserB4 is not need sprd_create_bootdevice.ko. \n",
			__func__);
		return 0;
	}

	bootdevice_dir = proc_mkdir("bootdevice", NULL);
	if (!bootdevice_dir) {
		pr_err("%s: failed to create /proc/bootdevice\n",
			__func__);
		return -1;
	}
	node = ARRAY_SIZE(sprd_emmc_node_info);

	for (i = 0; i < node; i++) {
		prEntry = proc_create(sprd_emmc_node_info[i], PROC_MODE, bootdevice_dir, proc_fops_list[i]);
		if (!prEntry) {
			pr_err("%s,failed to create node: /proc/bootdevice/%s\n",
				__func__, sprd_emmc_node_info[i]);
			return -1;
		}
	}

	return 0;
}
module_init(sprd_create_bootdevice_init);
MODULE_DESCRIPTION("bootdevice");
MODULE_LICENSE("GPL v2");