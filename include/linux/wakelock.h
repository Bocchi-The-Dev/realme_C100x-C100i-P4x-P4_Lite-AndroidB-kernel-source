/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * include/linux/wakelock.h - compatibility shim for the pre-5.12 wakelock API
 *
 * The old "struct wake_lock" interface was superseded by "struct wakeup_source"
 * and removed from mainline in 5.12.  This tree is 5.15, so the header does not
 * exist -- yet UNISOC vendor code still includes it, both out of tree under
 * kernel_modules/ and in tree under drivers/power/oplus/.  Without this header
 * those units fail with
 *
 *   fatal error: 'linux/wakelock.h' file not found
 *
 * which is how audio/sprd/codec/sprd/sc2730/codec (our PMIC audio codec) and
 * common/camera/core (sprd_camera) were failing to build.
 *
 * The mapping is not a guess.  This tree's drivers/base/power/wakeup.c still
 * has the list based wakeup_source_add() with no wakeup_sources[] index guard
 * (that guard arrived in a later upstream release), so an embedded struct
 * wakeup_source can be added and removed directly, and __pm_stay_awake() /
 * __pm_relax() are the stay_awake/relax primitives.  Both are declared by
 * <linux/pm.h> in every configuration, with empty stubs when CONFIG_PM_SLEEP is
 * off, so nothing here depends on PM_SLEEP being enabled.
 *
 * Surface: exactly the symbols this tree actually references, which was
 * determined by grepping drivers/ and kernel_modules/ for the pre-5.12 API.
 * That found wake_lock, wake_unlock, wake_lock_init, wake_lock_destroy,
 * wake_lock_timeout, wakeup_source_init and wakeup_source_trash, and nothing
 * else.  The old WAKEUP_SOURCE_INIT / WAKEUP_SOURCE_DECLARE / DEFINE_WAKE_LOCK
 * macros are deliberately NOT provided: no caller in this tree uses them, and
 * guessing their expansion would be inventing semantics rather than porting
 * them.  wakeup_source_create/destroy/add/remove already exist in 5.15 and are
 * not redefined here.
 *
 * This header is inert for anything that does not include it: it adds no code
 * to vmlinux and defines no CONFIG_ symbol.
 */

#ifndef _LINUX_WAKELOCK_H
#define _LINUX_WAKELOCK_H

/*
 * <linux/pm_wakeup.h> cannot be included directly -- it hard-errors unless
 * <linux/device.h> has already been pulled in (it wants struct device).
 * <linux/pm.h> is the supported entry point and includes both.
 */
#include <linux/pm.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>

/*
 * struct wake_lock - a named, statically allocated wakelock
 * @ws: the underlying wakeup source
 * @name: name reported to /sys/power/wakeup_sources
 * @count: nested wake_lock() count, mirrors the old lockcnt
 * @timeout_work: deferred relax used by wake_lock_timeout()
 *
 * The old API let callers hold a lock by value in a static struct, so this
 * wraps a wakeup_source rather than using wakeup_source_create().
 */
struct wake_lock {
	struct wakeup_source ws;
	const char	*name;
	int		count;
	struct delayed_work timeout_work;
};

/* Defined below, but needed by wake_lock_init() to arm timeout_work. */
static inline void __wake_lock_expire(struct work_struct *w);

/**
 * wakeup_source_init - initialise a bare wakeup source with a name
 * @ws: the wakeup source
 * @name: name reported to /sys/power/wakeup_sources
 *
 * 5.15 has no wakeup_source_init(); wakeup_source_add() is where the list
 * linkage happens, so this only records the name and clears the active flag.
 */
static inline void wakeup_source_init(struct wakeup_source *ws, const char *name)
{
	ws->name = name;
	ws->active = false;
}

/**
 * wakeup_source_trash - drop a wakeup source that is not held
 * @ws: the wakeup source
 *
 * The pre-5.12 counterpart of wakeup_source_destroy() for statically allocated
 * sources.  There is nothing to free, so this just unlinks it.
 */
static inline void wakeup_source_trash(struct wakeup_source *ws)
{
	wakeup_source_remove(ws);
}

/**
 * wake_lock_init - initialise a wakelock
 * @lock: the wakelock
 *
 * The old API required this before the first wake_lock(); several vendor
 * drivers still call it, so the source is left unadded here and added on the
 * first take.
 */
static inline void wake_lock_init(struct wake_lock *lock)
{
	lock->name = "unknown";
	lock->count = 0;
	wakeup_source_init(&lock->ws, lock->name);
	INIT_DELAYED_WORK(&lock->timeout_work, __wake_lock_expire);
}

/**
 * wake_lock_destroy - release the resources of a wakelock
 * @lock: the wakelock
 */
static inline void wake_lock_destroy(struct wake_lock *lock)
{
	wakeup_source_trash(&lock->ws);
	lock->name = NULL;
	lock->count = 0;
}

/**
 * __wake_lock - take a wakelock, reference counted
 * @lock: the wakelock
 */
static inline void __wake_lock(struct wake_lock *lock)
{
	lock->count++;
	if (lock->count == 1) {
		wakeup_source_add(&lock->ws);
		__pm_stay_awake(&lock->ws);
	}
}

/**
 * wake_lock - take a wakelock
 * @lock: the wakelock
 *
 * Reference counted, as the original was: N nested wake_lock() calls need N
 * wake_unlock() calls.
 */
static inline void wake_lock(struct wake_lock *lock)
{
	__wake_lock(lock);
}

/**
 * __wake_unlock - drop one reference on a wakelock
 * @lock: the wakelock
 */
static inline void __wake_unlock(struct wake_lock *lock)
{
	if (lock->count == 0)
		return;
	lock->count--;
	if (lock->count == 0) {
		__pm_relax(&lock->ws);
		wakeup_source_remove(&lock->ws);
	}
}

/**
 * wake_unlock - drop a wakelock
 * @lock: the wakelock
 */
static inline void wake_unlock(struct wake_lock *lock)
{
	__wake_unlock(lock);
}

/**
 * __wake_lock_expire - delayed_work callback that drops the reference
 * @w: the work item embedded in struct wake_lock
 *
 * Cannot be NULL: a delayed_work with a NULL function would oops the moment the
 * timeout expired.
 */
static inline void __wake_lock_expire(struct work_struct *w)
{
	struct wake_lock *lock = container_of(to_delayed_work(w),
					      struct wake_lock, timeout_work);
	__wake_unlock(lock);
}

/**
 * wake_lock_timeout - take a wakelock and auto-release it
 * @lock: the wakelock
 * @timeout: expiry in jiffies, 0 meaning "no expiry"
 *
 * The original armed a hrtimer.  A delayed_work is used because it is the
 * primitive that exists here and it keeps the same observable behaviour: the
 * reference is dropped once, after @timeout.  The unit is jiffies, as in the
 * original API -- callers such as silead_fp pass 10*HZ, not milliseconds.
 */
static inline void wake_lock_timeout(struct wake_lock *lock, long timeout)
{
	wake_lock(lock);
	if (timeout) {
		/* Re-arm: drop any previous expiry so the newest one wins. */
		cancel_delayed_work(&lock->timeout_work);
		schedule_delayed_work(&lock->timeout_work, timeout);
	}
}

#endif /* _LINUX_WAKELOCK_H */
