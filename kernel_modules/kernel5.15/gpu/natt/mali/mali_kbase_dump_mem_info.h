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

#ifndef _KBASE_DUMP_MEM_INFO_H
#define _KBASE_DUMP_MEM_INFO_H

#include <mali_kbase.h>

/**
 * kbase_mmu_fault_addr_info_show() - show mem info when gpu happend mmu fault.
 *
 * @kctx: The kbase context structure of the target process
 *
 * @fault_addr: the mmu fault address 
 */
void kbase_mmu_fault_addr_info_show(struct kbase_context *kctx, u64 fault_addr);

#endif /* _KBASE_DUMP_KATOM_H */
