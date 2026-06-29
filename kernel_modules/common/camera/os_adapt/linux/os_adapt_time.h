/*
 * Copyright (C) 2022-2023 UNISOC Communications Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _OS_ADAPT_TIMER_H_
#define _OS_ADAPT_TIMER_H_

#include <linux/version.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/delay.h>
#include <linux/ktime.h>
#include <linux/timer.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
typedef struct timespec64 timespec;
#define ktime_get_ts ktime_get_ts64
typedef struct __kernel_old_timeval timeval;
#else
typedef struct timespec timespec;
typedef struct timeval timeval;
#endif


#define os_find_container(var, callback_timer, timer_fieldname) \
     from_timer(var, callback_timer, timer_fieldname)

typedef void (*timer_callback_func)(struct timer_list *t);

static inline  ktime_t os_adapt_time_sub_ns(ktime_t kt, ktime_t nsval)
{
     return ktime_sub_ns(kt, nsval);
}

static inline ktime_t os_adapt_time_sub(ktime_t lhs, ktime_t rhs)
{
     return ktime_sub(lhs, rhs);
}

static inline ktime_t os_adapt_time_get_monotonic(void)
{
     return ktime_get();
}

static inline ktime_t os_adapt_time_get_boottime(void)
{
     return ktime_get_boottime();
}


static inline void os_adapt_time_get_ts(timespec *ts)
{
     ktime_get_ts(ts);
}

 static inline int os_adapt_time_mod_timer(struct timer_list *timer, unsigned long expires)
{
     return mod_timer(timer, expires);
}

static inline void os_adapt_time_del_timer_sync(struct timer_list *cam_timer)
{
     del_timer_sync(cam_timer);
}

static inline void os_adapt_time_udelay(unsigned int x)
{
     udelay(x);
}

static inline void os_adapt_time_mdelay(unsigned int x)
{
     mdelay(x);
}

static inline void os_adapt_time_msleep(unsigned int x)
{
     msleep(x);
}

static inline void os_adapt_time_usleep_range(unsigned int x, unsigned int y)
{
     usleep_range(x, y);
}

/********************** kernel 5.15 ************************/
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
static inline timeval os_adapt_time_ktime_to_timeval(const ktime_t kt)
{
     timespec temp_timespec = {0};
     timeval temp_timeval = {0};

     temp_timespec = ktime_to_timespec64(kt);
     temp_timeval.tv_sec = temp_timespec.tv_sec;
     temp_timeval.tv_usec = temp_timespec.tv_nsec / 1000L;

     return temp_timeval;
}

static inline timespec os_adapt_time_timespec_sub(timespec lhs, timespec rhs)
{
     return timespec64_sub(lhs,rhs);
}

static inline void os_adapt_time_init_timer(struct timer_list *cam_timer,
     timer_callback_func func, unsigned long data)
{
     data = 0;
     timer_setup(cam_timer, func, data);
}
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
static inline timeval os_adapt_time_ktime_to_timeval(const ktime_t kt)
{
     return ktime_to_timeval(kt);
}

static inline timespec os_adapt_time_timespec_sub(struct timespec lhs, struct timespec rhs)
{
     return timespec64_to_timespec(timespec64_sub(
          timespec_to_timespec64(lhs),
          timespec_to_timespec64(rhs)));
}

static inline void os_adapt_time_init_timer(struct timer_list *cam_timer,
     timer_callback_func func, unsigned long data)
{
     data = 0;
     timer_setup(cam_timer, func, data);
}
#endif

/********************** kernel 4.14 ************************/
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))

static inline timeval os_adapt_time_ktime_to_timeval(const ktime_t kt)
{
     return ktime_to_timeval(kt);
}

static inline timespec os_adapt_time_timespec_sub(timespec lhs, timespec rhs)
{
     return timespec_sub(lhs, rhs);
}

static inline void os_adapt_time_init_timer(struct timer_list *cam_timer,
     timer_callback_func func, unsigned long data)
{
     setup_timer(cam_timer, func, data);
}
#endif

#endif
