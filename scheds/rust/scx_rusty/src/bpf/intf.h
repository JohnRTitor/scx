// Copyright (c) Meta Platforms, Inc. and affiliates.

// This software may be used and distributed according to the terms of the
// GNU General Public License version 2.
#ifndef __INTF_H
#define __INTF_H

#include <stdbool.h>
#ifndef __kptr
#ifdef __KERNEL__
#error "__kptr_ref not defined in the kernel"
#endif
#define __kptr
#endif

#ifndef __KERNEL__
typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long long u64;
#endif

#ifdef LSP
#define __bpf__
#include "../../../../include/scx/ravg.bpf.h"
#else
#include <scx/ravg.bpf.h>
#endif

enum consts {
	MAX_CPUS		= 512,
	MAX_DOMS		= 64,	/* limited to avoid complex bitmask ops */
	MAX_NUMA_NODES		= MAX_DOMS,	/* Assume at least 1 domain per NUMA node */
	CACHELINE_SIZE		= 64,
	NO_DOM_FOUND		= MAX_DOMS + 1,

	LB_DEFAULT_WEIGHT	= 100,
	LB_MIN_WEIGHT		= 1,
	LB_MAX_WEIGHT		= 10000,
	LB_LOAD_BUCKETS		= 100,	/* Must be a factor of LB_MAX_WEIGHT */
	LB_WEIGHT_PER_BUCKET	= LB_MAX_WEIGHT / LB_LOAD_BUCKETS,

	/* Time constants */
	MSEC_PER_SEC		= 1000LLU,
	USEC_PER_MSEC		= 1000LLU,
	NSEC_PER_USEC		= 1000LLU,
	NSEC_PER_MSEC           = USEC_PER_MSEC * NSEC_PER_USEC,
	USEC_PER_SEC            = USEC_PER_MSEC * MSEC_PER_SEC,
	NSEC_PER_SEC            = NSEC_PER_USEC * USEC_PER_SEC,

	/* Constants used for determining a task's deadline */
	DL_RUNTIME_SCALE	= 2, /* roughly scales average runtime to */
				     /* same order of magnitude as waker  */
				     /* and blocked frequencies */
	DL_MAX_LATENCY_NS	= (50 * NSEC_PER_MSEC),
	DL_FREQ_FT_MAX		= 100000,
	DL_MAX_LAT_PRIO		= 39,

	/*
	 * When userspace load balancer is trying to determine the tasks to push
	 * out from an overloaded domain, it looks at the the following number
	 * of recently active tasks of the domain. While this may lead to
	 * spurious migration victim selection failures in pathological cases,
	 * this isn't a practical problem as the LB rounds are best-effort
	 * anyway and will be retried until loads are balanced.
	 */
	MAX_DOM_ACTIVE_TPTRS	= 1024,
};

/* Statistics */
enum stat_idx {
	/* The following fields add up to all dispatched tasks */
	RUSTY_STAT_WAKE_SYNC,
	RUSTY_STAT_SYNC_PREV_IDLE,
	RUSTY_STAT_PREV_IDLE,
	RUSTY_STAT_GREEDY_IDLE,
	RUSTY_STAT_PINNED,
	RUSTY_STAT_DIRECT_DISPATCH,
	RUSTY_STAT_DIRECT_GREEDY,
	RUSTY_STAT_DIRECT_GREEDY_FAR,
	RUSTY_STAT_DSQ_DISPATCH,
	RUSTY_STAT_GREEDY_LOCAL,
	RUSTY_STAT_GREEDY_XNUMA,

	/* Extra stats that don't contribute to total */
	RUSTY_STAT_REPATRIATE,
	RUSTY_STAT_KICK_GREEDY,
	RUSTY_STAT_LOAD_BALANCE,

	/* Errors */
	RUSTY_STAT_TASK_GET_ERR,

	/* Deadline related stats */
	RUSTY_STAT_DL_CLAMP,
	RUSTY_STAT_DL_PRESET,

	RUSTY_NR_STATS,
};

/*
 * XXXETSAL This is convoluted for a reason. We have a three way conflict here:
 *
 * 1) intf.h gets parsed by bindgen, and I guess bindgen does not fully
 * preprocess the file because it does not resolve __arena to
 * __attribute__((address_space(1))) and errors out. So we cannot use the
 * __arena macro from bpf_arena_common.h that involves all this #ifdef'ing.
 * 2) Clang 18 full-on crashes if we pass address_space(1) as an attribute as it
 * is unable to generate the address space conversion instruction.
 * 3) Clang 19 requires the address_space attribute because address space
 * conversions can only be implicit.
 *
 * The below snippet replicates as tersely as possible the logic in
 * bpf_arena_common.h to get the code passing with both Clang 18 and 19.
 */

struct dom_ctx;
#if defined(__BPF_FEATURE_ADDR_SPACE_CAST) && !defined(BPF_ARENA_FORCE_ASM)
typedef struct dom_ctx __attribute__((address_space(1))) *dom_ptr;
#else
typedef struct dom_ctx *dom_ptr;
#endif

struct task_ctx {
	/* The domains this task can run on */
	u64 dom_mask;
	u64 preferred_dom_mask;

	/* Arena pointer to this task's domain. */
	dom_ptr domc;

	u32 target_dom;
	u32 weight;
	bool runnable;
	u64 dom_active_tasks_gen;
	u64 deadline;

	u64 sum_runtime;
	u64 avg_runtime;
	u64 last_run_at;

	/* frequency with which a task is blocked (consumer) */
	u64 blocked_freq;
	u64 last_blocked_at;

	/* frequency with which a task wakes other tasks (producer) */
	u64 waker_freq;
	u64 last_woke_at;

	/* The task is a workqueue worker thread */
	bool is_kworker;

	/* Allowed on all CPUs and eligible for DIRECT_GREEDY optimization */
	bool all_cpus;

	/* select_cpu() telling enqueue() to queue directly on the DSQ */
	bool dispatch_local;

	struct ravg_data dcyc_rd;
};

/* XXXETSAL Same rationale as for dom_ptr. Remove once we dump Clang 18.*/

#if defined(__BPF_FEATURE_ADDR_SPACE_CAST) && !defined(BPF_ARENA_FORCE_ASM)
typedef struct task_ctx __attribute__((address_space(1))) *task_ptr;
#else
typedef struct task_ctx *task_ptr;
#endif

struct bucket_ctx {
	u64 dcycle;
	struct ravg_data rd;
};

struct dom_active_tasks {
	u64 gen;
	u64 read_idx;
	u64 write_idx;
	task_ptr tasks[MAX_DOM_ACTIVE_TPTRS];
};

struct dom_ctx {
	u32 id;
	u64 min_vruntime;

	u64 dbg_dcycle_printed_at;
	struct bucket_ctx buckets[LB_LOAD_BUCKETS];
	struct dom_active_tasks active_tasks;
};

struct node_ctx {
	struct bpf_cpumask __kptr *cpumask;
};

#endif /* __INTF_H */
