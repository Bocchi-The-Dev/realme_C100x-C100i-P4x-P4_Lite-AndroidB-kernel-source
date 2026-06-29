/*
 * Copyright (c) 2012-2013 by Tensilica Inc. ALL RIGHTS RESERVED.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "elf.h"
#include "xt_library_loader.h"
#include "loader_internal.h"
#include <linux/bsearch.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/sort.h>

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "sprd-vdsp: common %d %d %s : "\
        fmt, current->pid, __LINE__, __func__

xtlib_loader_globals xtlib_globals;

int xtlib_verify_magic(Elf32_Ehdr *header)
{
	Elf32_Byte magic_no;

	magic_no = header->e_ident[EI_MAG0];
	if (unlikely(magic_no != 0x7f)) {
		pr_err("error 0\n");
		return -1;
	}

	magic_no = header->e_ident[EI_MAG1];
	if (unlikely(magic_no != 'E')) {
		pr_err("error 1\n");
		return -1;
	}

	magic_no = header->e_ident[EI_MAG2];
	if (unlikely(magic_no != 'L')) {
		pr_err("error 2\n");
		return -1;
	}

	magic_no = header->e_ident[EI_MAG3];
	if (unlikely(magic_no != 'F')) {
		pr_err("error 3\n");
		return -1;
	}

	if (unlikely(header->e_ident[EI_CLASS] != ELFCLASS32)) {
		pr_err("error 4\n");
		return -1;
	}
	{
		/* determine byte order  */
		union {
			short s;
			char c[sizeof(short)];
		} u;

		u.s = 1;

		if (header->e_ident[EI_DATA] == ELFDATA2LSB) {
			xtlib_globals.byteswap = u.c[sizeof(short) - 1] == 1;
		} else if (header->e_ident[EI_DATA] == ELFDATA2MSB) {
			xtlib_globals.byteswap = u.c[0] == 1;
		} else {
			pr_err("error 5\n");
			return -1;
		}
	}

	return 0;
}

void xtlib_load_seg(Elf32_Phdr *pheader, void *src_addr, xt_ptr dst_addr,
	memcpy_func_ex mcpy, memset_func_ex mset, void *user)
{
	Elf32_Word bytes_to_copy = xtlib_host_word(pheader->p_filesz);
	Elf32_Word bytes_to_zero = xtlib_host_word(pheader->p_memsz) - bytes_to_copy;
	char *pindex = (char *)user;
	xt_ptr zero_addr = 0;
	void *zero_addr_ap = pindex + bytes_to_copy;

	if (bytes_to_copy > 0) {
		mcpy(dst_addr, src_addr, bytes_to_copy, user);
	}

	if (bytes_to_zero > 0)
		mset(zero_addr, 0, bytes_to_zero, (void *)zero_addr_ap);
}

unsigned int xtlib_error(void)
{
	return xtlib_globals.err;
}

Elf32_Half xtlib_host_half(Elf32_Half v)
{
	return (xtlib_globals.byteswap) ? (v >> 8) | (v << 8) : v;
}

Elf32_Word xtlib_host_word(Elf32_Word v)
{
	if (xtlib_globals.byteswap) {
		v = ((v & 0x00FF00FF) << 8) | ((v & 0xFF00FF00) >> 8);
		v = (v >> 16) | (v << 16);
	}
	return v;
}

