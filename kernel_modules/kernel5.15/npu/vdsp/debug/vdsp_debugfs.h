/**
 * SPDX-FileContributor: vdsp driver debug function.
 */

#ifndef __VDSP_DEBUGFS_H__
#define __VDSP_DEBUGFS_H__

int vdsp_debugfs_init(void);
void vdsp_debugfs_exit(void);
unsigned int vdsp_debugfs_log_mode(void);
unsigned int vdsp_debugfs_log_level(void);
unsigned int vdsp_debugfs_dvfs_level(void);
unsigned int vdsp_debugfs_trace_mem(void);
unsigned int vdsp_debugfs_trace_iommu(void);
unsigned int vdsp_debugfs_trace_hw(void);
unsigned int vdsp_debugfs_trace_firmware(void);
unsigned int vdsp_debugfs_cmd_timeout(void);
#endif

