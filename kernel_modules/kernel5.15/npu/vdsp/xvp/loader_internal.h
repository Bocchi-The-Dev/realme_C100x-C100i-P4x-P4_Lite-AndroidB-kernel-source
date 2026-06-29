
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

typedef struct xtlib_loader_globals {
	int err;
	int byteswap;
} xtlib_loader_globals;

extern xtlib_loader_globals xtlib_globals;

typedef struct {
	memcpy_func mcpy;
	memset_func mset;
} user_funcs;

int xtlib_verify_magic(Elf32_Ehdr *header);

void
xtlib_load_seg(Elf32_Phdr *pheader, void *src_addr,
	xt_ptr dst_addr, memcpy_func_ex mcpy,
	memset_func_ex mset, void *user);

Elf32_Half xtlib_host_half(Elf32_Half v);

Elf32_Word xtlib_host_word(Elf32_Word v);

#define xtlib_xt_half  xtlib_host_half
#define xtlib_xt_word  xtlib_host_word

