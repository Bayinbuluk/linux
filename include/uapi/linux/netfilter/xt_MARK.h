/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _XT_MARK_H
#define _XT_MARK_H

#include <linux/types.h>

struct xt_mark_tginfo2 {
	__u32 mark, mask;
};

struct x