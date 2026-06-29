/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * This file has been modified by Unisoc (Shanghai) Technologies Co., Ltd in 2024.
 *
 * (C) COPYRIGHT 2012-2023 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#ifndef _KBASE_DUMP_KATOM_H
#define _KBASE_DUMP_KATOM_H

#include <mali_kbase.h>

/**
 * kbase_dump_atoms() - UMD want to get the katom info about the cmar_command.
 *
 * @kbdev: The kbase device structure of the device
 *
 * @device: the cmar_device belonged to 
 *
 * @command: the cmar_command the katom corresponding to
 *
 * This function must be called once only when a kbase device is initialized.
 *
 * Return: 0 on success. Error code (negative) on failure.
 */
int kbase_dump_atoms(struct kbase_context *kctx, u64 device, u64 command);

/**
 * kbase_atom_dump_reset() - Clear the information storaged in struct kbase_jd_atom before atom reuse.
 *
 * @katom: which katom need to been clean
 *
 * Return: NULL.
 */
void kbase_atom_dump_reset(struct kbase_jd_atom *katom);

/**
 * kbase_active_dump_atoms() - The KMD check the status of katom automatically.
 *
 * @katom: which katom need to been check
 *
 * @timeout: the time limits for the different stage of katom processing 
 *
 * This function must be called once only when a kbase device is initialized.
 *
 * return ture indicates dump successfully.
 */
bool kbase_active_dump_atoms(struct kbase_jd_atom *katom, u64 timeout);

/**
 * kbase_dump_dma_fence_array() - dump the dma fene array info.
 *
 * @kbdev: The kbase device structure of the device
 *
 * @fence: the dma fence array that need to be checked
 *
 * This function must be called once only when a kbase device is initialized.
 *
 * Return: NULL.
 */
//void kbase_dump_dma_fence_array(struct kbase_device *kbdev, struct dma_fence *head);

/**
 * kbase_dump_dma_fence() - dump the dma fene info.
 *
 * @kbdev: The kbase device structure of the device
 *
 * @fence: the dma fence that need to be checked
 *
 * This function must be called once only when a kbase device is initialized.
 *
 * Return: NULL.
 */
void kbase_dump_dma_fence(struct kbase_device *kbdev, struct dma_fence *fence);
#endif /* _KBASE_DUMP_KATOM_H */
