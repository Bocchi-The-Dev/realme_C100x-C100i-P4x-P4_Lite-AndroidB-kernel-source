/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2023 UNISOC. All rights reserved.
 */
#ifndef _FG_UID_H
#define _FG_UID_H

struct fg_info {
    int fg_num;
    int fg_uids;
};

bool is_fg(int uid);

#endif /*_FG_UID_H*/

