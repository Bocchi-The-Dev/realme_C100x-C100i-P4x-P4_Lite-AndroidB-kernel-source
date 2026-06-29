/*
 *
 * SPDX-License-Identifier: LicenseRef-Unisoc-General-1.0

 * Copyright 2016-2023 Unisoc (Shanghai) Technologies Co., Ltd

 * Licensed under the Unisoc General Software License, version 1.0 (the License);
 * you may not use this file except in compliance with the License. You may obtain a copy of the License at

 * https://www.unisoc.com/en_us/license/UNISOC_GENERAL_LICENSE_V1.0-EN_US

 * Software distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OF ANY KIND, either express or implied.
 * See the Unisoc General Software License, version 1.0 for more details.
 *
 */

#include <mali_kbase_dump_mem_info.h>


/**
 * find_region_in_zone - Show information from specific rbtree
 * @zone: The memory zone to be displayed
 * @fault_addr: The target addr
 * @tag_reg: where to storage target region
 *
 * This function is called to show information about the region which contains the fault_addr
 */
static bool find_region_in_zone(struct kbase_reg_zone *zone, u64 fault_addr, struct kbase_va_region **tag_reg)
{
	struct rb_node *p;
	struct rb_root *rbtree = &zone->reg_rbtree;
	struct kbase_va_region *reg = NULL;
	u64 reg_start_addr;
	u64 reg_end_addr;

	for (p = rb_first(rbtree); p; p = rb_next(p)) {
		reg = rb_entry(p, struct kbase_va_region, rblink);
		if (!(reg->flags & KBASE_REG_FREE)) {
			reg_start_addr = reg->start_pfn << PAGE_SHIFT;
			reg_end_addr = (reg->start_pfn << PAGE_SHIFT) + (reg->nr_pages << PAGE_SHIFT);
			if ((reg_start_addr <= fault_addr) && (fault_addr < reg_end_addr)) {
				*tag_reg = reg;
				return true;
			}
		}
	}
	return false;
}

void kbase_traverse_mem_trees(struct kbase_context *kctx)
{
	struct kbase_reg_zone *zone;
	struct rb_node *p;
	struct kbase_va_region *reg = NULL;
	struct rb_root *rbtree;
	enum kbase_memory_zone zone_idx;
	const char *type_names[5] = { "Native", "Imported UMM", "Imported user buf", "Alias",
				      "Raw" };

	kbase_gpu_vm_lock(kctx);
	for (zone_idx = 0; zone_idx < CONTEXT_ZONE_MAX; zone_idx++) {

		zone = &kctx->reg_zone[zone_idx];
		rbtree = &zone->reg_rbtree;
		for (p = rb_first(rbtree); p; p = rb_next(p)) {
			reg = rb_entry(p, struct kbase_va_region, rblink);
			if (!(reg->flags & KBASE_REG_FREE)) {
				dev_err(kctx->kbdev->dev, "mali_mem_debug:VA:0x%10llx VAsize:0x%8zx CmtSize:0x%8zx Flags:0x%5lx Type:%s\n",
					reg->start_pfn << PAGE_SHIFT, reg->nr_pages << PAGE_SHIFT,
					kbase_reg_current_backed_size(reg) << PAGE_SHIFT, reg->flags,
					type_names[reg->gpu_alloc->type]);
			}
		}
	}
	kbase_gpu_vm_unlock(kctx);
}

/**
 * kbase_mmu_fault_addr_info_show - Show information about the memory region which happened page fault in
 */
void kbase_mmu_fault_addr_info_show(struct kbase_context *kctx, u64 fault_addr)
{


	enum kbase_memory_zone zone_idx;

	static ktime_t last_dump = 0;

	bool found = false;
	struct kbase_va_region *reg = NULL;
	const char *type_names[5] = { "Native", "Imported UMM", "Imported user buf", "Alias",
				      "Raw" };
	unsigned long dump_period_ns = 10000000000UL;
	ktime_t cur_t = ktime_get();

	kbase_gpu_vm_lock(kctx);
	for (zone_idx = 0; zone_idx < CONTEXT_ZONE_MAX; zone_idx++) {
		struct kbase_reg_zone *zone;

		zone = &kctx->reg_zone[zone_idx];
		if (find_region_in_zone(zone, fault_addr, &reg)) {
			found = true;
			break;
		}
	}
	kbase_gpu_vm_unlock(kctx);

	if (found) {
		dev_err(kctx->kbdev->dev, "mali_mem_debug:fault_addr:0x%llx, VA:0x%16llx, VAsize:0x%16zx, CmtSize:0x%16zx, Flags:0x%8lx, type:%s\n",
			fault_addr, reg->start_pfn << PAGE_SHIFT, reg->nr_pages << PAGE_SHIFT,
			kbase_reg_current_backed_size(reg) << PAGE_SHIFT, reg->flags,
			type_names[reg->gpu_alloc->type]);
	} else {
		dev_err(kctx->kbdev->dev, "mali_mem_debug: there is no region cover this addr:0x%llx.", fault_addr);
		if ((cur_t - last_dump) > dump_period_ns) {
			kbase_traverse_mem_trees(kctx);
			last_dump = cur_t;
		}

	}
}