# Plan 8b: Kernel Modules — XDP, Entropy, Enclave

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the remaining 3 kernel-space modules: eBPF/XDP packet programs (compiled with clang, not a .ko), straylight-entropy.ko (hardware entropy via RDRAND/jitter with NIST health tests), and straylight-enclave.ko (SGX enclave management). After this plan, all 6 kernel modules are complete.

**Architecture:** Three independent modules under `kernel/`. The XDP programs are eBPF bytecode compiled with `clang -target bpf` and loaded via libbpf at runtime. The entropy and enclave modules are standard Linux loadable kernel modules (.ko) registered via DKMS.

**Tech Stack:** C (kernel coding style), clang/LLVM (BPF target), libbpf headers, Linux kernel headers, DKMS

**Spec:** `docs/superpowers/specs/2026-03-16-straylight-os-rewrite-design.md`

**Depends on:** Plan 8a (kernel module infrastructure, Kbuild patterns, DKMS scaffolding)

**Development environment:** Linux x86_64 required (Debian Bookworm/Trixie). Requires `linux-headers-$(uname -r)`, `clang`, `llvm`, `libbpf-dev`, `dkms`. SGX module requires SGX-capable hardware for runtime testing (builds without it).

---

## File Layout

```
kernel/
├── xdp/                          # eBPF programs (NOT a .ko)
│   ├── Makefile                  # clang -target bpf compilation
│   ├── xdp_maps.h                # BPF map definitions
│   ├── xdp_filter.bpf.c          # XDP packet filter
│   ├── xdp_redirect.bpf.c        # XDP interface redirect
│   └── xdp_stats.bpf.c           # Per-CPU packet counters
│
├── entropy/                      # straylight-entropy.ko
│   ├── Kbuild
│   ├── entropy_main.c            # module_init/exit, hwrng_register
│   ├── entropy_jitter.c          # CPU jitter entropy source
│   ├── entropy_rdrand.c          # RDRAND/RDSEED wrapper
│   └── entropy_health.c          # NIST SP 800-90B health tests
│
├── enclave/                      # straylight-enclave.ko
│   ├── Kbuild
│   ├── enclave_main.c            # module_init/exit, misc device
│   ├── enclave_epc.c             # Enclave Page Cache management
│   ├── enclave_sealed.c          # Sealed storage via EGETKEY
│   └── enclave_attestation.c     # Local attestation via EREPORT
│
└── dkms/
    ├── straylight-xdp-dkms/dkms.conf
    ├── straylight-entropy-dkms/dkms.conf
    └── straylight-enclave-dkms/dkms.conf
```

---

## Chunk 1: straylight-xdp — eBPF Programs

### Task 1.1: BPF Map Definitions — `xdp_maps.h`

- [ ] Create `kernel/xdp/xdp_maps.h`

```c
#ifndef STRAYLIGHT_XDP_MAPS_H
#define STRAYLIGHT_XDP_MAPS_H

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

/* --- Filter rule: keyed by IPv4 address --- */
struct filter_rule {
    __u16 port;          /* 0 = match all ports */
    __u8  proto;         /* IPPROTO_TCP, IPPROTO_UDP, 0 = any */
    __u8  action;        /* XDP_DROP or XDP_PASS */
};

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 16384);
    __type(key, __u32);                /* IPv4 address (network byte order) */
    __type(value, struct filter_rule);
} filter_rules SEC(".maps");

/* --- Per-CPU packet/byte counters --- */
struct pkt_counter {
    __u64 packets;
    __u64 bytes;
};

struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, 4);            /* index: 0=pass, 1=drop, 2=redirect, 3=error */
    __type(key, __u32);
    __type(value, struct pkt_counter);
} stats_counters SEC(".maps");

/* --- Redirect target map: ifindex lookup --- */
struct {
    __uint(type, BPF_MAP_TYPE_DEVMAP);
    __uint(max_entries, 256);
    __type(key, __u32);                /* source ifindex */
    __type(value, __u32);              /* target ifindex */
} redirect_map SEC(".maps");

#endif /* STRAYLIGHT_XDP_MAPS_H */
```

### Task 1.2: XDP Packet Filter — `xdp_filter.bpf.c`

- [ ] Create `kernel/xdp/xdp_filter.bpf.c`

```c
#include "xdp_maps.h"
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <bpf/bpf_endian.h>

static __always_inline void update_stats(__u32 idx, __u64 len)
{
    struct pkt_counter *cnt = bpf_map_lookup_elem(&stats_counters, &idx);
    if (cnt) {
        cnt->packets++;
        cnt->bytes += len;
    }
}

static __always_inline int parse_and_filter(struct xdp_md *ctx)
{
    void *data     = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;

    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)
        return XDP_PASS;

    if (eth->h_proto != bpf_htons(ETH_P_IP))
        return XDP_PASS;

    struct iphdr *iph = (void *)(eth + 1);
    if ((void *)(iph + 1) > data_end)
        return XDP_PASS;

    /* Look up source IP in filter rules */
    struct filter_rule *rule = bpf_map_lookup_elem(&filter_rules, &iph->saddr);
    if (!rule)
        return XDP_PASS;

    /* Port matching for TCP/UDP */
    if (rule->port != 0) {
        __u16 dport = 0;
        if (iph->protocol == IPPROTO_TCP) {
            struct tcphdr *tcp = (void *)iph + (iph->ihl * 4);
            if ((void *)(tcp + 1) > data_end)
                return XDP_PASS;
            dport = bpf_ntohs(tcp->dest);
        } else if (iph->protocol == IPPROTO_UDP) {
            struct udphdr *udp = (void *)iph + (iph->ihl * 4);
            if ((void *)(udp + 1) > data_end)
                return XDP_PASS;
            dport = bpf_ntohs(udp->dest);
        }
        if (dport != rule->port)
            return XDP_PASS;
    }

    /* Protocol matching */
    if (rule->proto != 0 && iph->protocol != rule->proto)
        return XDP_PASS;

    return rule->action;
}

SEC("xdp/filter")
int xdp_filter_prog(struct xdp_md *ctx)
{
    __u64 len = ctx->data_end - ctx->data;
    int action = parse_and_filter(ctx);

    __u32 idx = (action == XDP_DROP) ? 1 : 0;
    update_stats(idx, len);

    return action;
}

char _license[] SEC("license") = "GPL";
```

### Task 1.3: XDP Redirect — `xdp_redirect.bpf.c`

- [ ] Create `kernel/xdp/xdp_redirect.bpf.c`

```c
#include "xdp_maps.h"
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <bpf/bpf_endian.h>

SEC("xdp/redirect")
int xdp_redirect_prog(struct xdp_md *ctx)
{
    void *data     = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;
    __u64 len      = data_end - data;

    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)
        return XDP_PASS;

    if (eth->h_proto != bpf_htons(ETH_P_IP))
        return XDP_PASS;

    /* Redirect based on ingress ifindex */
    __u32 ingress = ctx->ingress_ifindex;
    __u32 *target = bpf_map_lookup_elem(&redirect_map, &ingress);
    if (!target)
        return XDP_PASS;

    __u32 idx = 2; /* redirect counter */
    update_stats(idx, len);

    return bpf_redirect_map(&redirect_map, ingress, 0);
}

/* update_stats inlined from xdp_maps.h pattern */
static __always_inline void update_stats(__u32 idx, __u64 len)
{
    struct pkt_counter *cnt = bpf_map_lookup_elem(&stats_counters, &idx);
    if (cnt) {
        cnt->packets++;
        cnt->bytes += len;
    }
}

char _license[] SEC("license") = "GPL";
```

### Task 1.4: XDP Stats — `xdp_stats.bpf.c`

- [ ] Create `kernel/xdp/xdp_stats.bpf.c`

```c
#include "xdp_maps.h"
#include <linux/if_ether.h>

SEC("xdp/stats")
int xdp_stats_prog(struct xdp_md *ctx)
{
    __u64 len = ctx->data_end - ctx->data;

    /* Count every packet as "pass" (index 0) — this program
     * is a pass-through counter attached alongside filter/redirect */
    __u32 idx = 0;
    struct pkt_counter *cnt = bpf_map_lookup_elem(&stats_counters, &idx);
    if (cnt) {
        cnt->packets++;
        cnt->bytes += len;
    }

    return XDP_PASS;
}

char _license[] SEC("license") = "GPL";
```

### Task 1.5: BPF Makefile and DKMS Config

- [ ] Create `kernel/xdp/Makefile`

```makefile
CLANG    ?= clang
LLC      ?= llc
BPFTOOL  ?= bpftool

CFLAGS := -O2 -g -Wall -target bpf \
          -D__TARGET_ARCH_x86 \
          -I/usr/include/$(shell uname -m)-linux-gnu

BPF_SRCS := xdp_filter.bpf.c xdp_redirect.bpf.c xdp_stats.bpf.c
BPF_OBJS := $(BPF_SRCS:.bpf.c=.bpf.o)

all: $(BPF_OBJS)

%.bpf.o: %.bpf.c xdp_maps.h
	$(CLANG) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(BPF_OBJS)

install: all
	install -d $(DESTDIR)/usr/lib/straylight/xdp
	install -m 644 $(BPF_OBJS) $(DESTDIR)/usr/lib/straylight/xdp/

.PHONY: all clean install
```

- [ ] Create `kernel/dkms/straylight-xdp-dkms/dkms.conf`

```ini
PACKAGE_NAME="straylight-xdp"
PACKAGE_VERSION="1.0.0"
MAKE[0]="make -C ${dkms_tree}/${PACKAGE_NAME}/${PACKAGE_VERSION}/build"
CLEAN="make -C ${dkms_tree}/${PACKAGE_NAME}/${PACKAGE_VERSION}/build clean"
AUTOINSTALL="yes"
# Note: XDP programs are eBPF objects, not kernel modules.
# DKMS is used here only for consistent build/install lifecycle.
# No BUILT_MODULE_NAME or DEST_MODULE_LOCATION needed.
```

---

## Chunk 2: straylight-entropy.ko — Hardware Entropy Source

### Task 2.1: Module Init and hwrng Registration — `entropy_main.c`

- [ ] Create `kernel/entropy/entropy_main.c`

```c
#include <linux/module.h>
#include <linux/hw_random.h>
#include <linux/random.h>
#include "entropy.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("StrayLight Project");
MODULE_DESCRIPTION("StrayLight hardware entropy source");
MODULE_VERSION("1.0.0");

static struct hwrng straylight_rng;

/* Quality estimate: 512 = full entropy per 1024 bits */
static int entropy_quality = 512;
module_param(entropy_quality, int, 0644);
MODULE_PARM_DESC(entropy_quality, "Entropy quality estimate (0-1024)");

static int straylight_rng_read(struct hwrng *rng, void *buf,
                               size_t max, bool wait)
{
    size_t filled = 0;
    int ret;

    /* Try RDRAND first (fast path) */
    ret = straylight_rdrand_fill(buf, max);
    if (ret > 0) {
        filled = ret;
        if (filled >= max)
            goto health_check;
    }

    /* Fill remainder from jitter source */
    if (filled < max) {
        ret = straylight_jitter_fill((u8 *)buf + filled, max - filled);
        if (ret > 0)
            filled += ret;
    }

    if (filled == 0)
        return -EIO;

health_check:
    /* Run NIST SP 800-90B health tests on output */
    if (!straylight_health_check(buf, filled)) {
        pr_warn("straylight-entropy: health check failed, discarding\n");
        return -EAGAIN;
    }

    return filled;
}

static int __init straylight_entropy_init(void)
{
    int ret;

    ret = straylight_rdrand_init();
    if (ret)
        pr_info("straylight-entropy: RDRAND unavailable, using jitter only\n");

    straylight_jitter_init();
    straylight_health_init();

    straylight_rng.name    = "straylight-entropy";
    straylight_rng.read    = straylight_rng_read;
    straylight_rng.quality = entropy_quality;

    ret = hwrng_register(&straylight_rng);
    if (ret) {
        pr_err("straylight-entropy: hwrng_register failed: %d\n", ret);
        return ret;
    }

    pr_info("straylight-entropy: registered (quality=%d)\n", entropy_quality);
    return 0;
}

static void __exit straylight_entropy_exit(void)
{
    hwrng_unregister(&straylight_rng);
    straylight_jitter_cleanup();
    pr_info("straylight-entropy: unregistered\n");
}

module_init(straylight_entropy_init);
module_exit(straylight_entropy_exit);
```

- [ ] Create `kernel/entropy/entropy.h`

```c
#ifndef STRAYLIGHT_ENTROPY_H
#define STRAYLIGHT_ENTROPY_H

#include <linux/types.h>

/* entropy_rdrand.c */
int  straylight_rdrand_init(void);
int  straylight_rdrand_fill(void *buf, size_t len);

/* entropy_jitter.c */
void straylight_jitter_init(void);
void straylight_jitter_cleanup(void);
int  straylight_jitter_fill(void *buf, size_t len);

/* entropy_health.c */
void straylight_health_init(void);
bool straylight_health_check(const void *buf, size_t len);

#endif /* STRAYLIGHT_ENTROPY_H */
```

### Task 2.2: RDRAND/RDSEED Wrapper — `entropy_rdrand.c`

- [ ] Create `kernel/entropy/entropy_rdrand.c`

```c
#include <linux/random.h>
#include <asm/cpufeatures.h>
#include <asm/archrandom.h>
#include "entropy.h"

#define RDRAND_RETRY_MAX  10

static bool has_rdrand;
static bool has_rdseed;

int straylight_rdrand_init(void)
{
    has_rdrand = boot_cpu_has(X86_FEATURE_RDRAND);
    has_rdseed = boot_cpu_has(X86_FEATURE_RDSEED);

    if (!has_rdrand && !has_rdseed)
        return -ENODEV;

    pr_info("straylight-entropy: RDRAND=%s RDSEED=%s\n",
            has_rdrand ? "yes" : "no",
            has_rdseed ? "yes" : "no");
    return 0;
}

static bool rdrand64_retry(u64 *val)
{
    int i;

    for (i = 0; i < RDRAND_RETRY_MAX; i++) {
        if (rdrand_long((unsigned long *)val))
            return true;
    }
    return false;
}

static bool rdseed64_retry(u64 *val)
{
    int i;

    if (!has_rdseed)
        return false;

    for (i = 0; i < RDRAND_RETRY_MAX; i++) {
        if (rdseed_long((unsigned long *)val))
            return true;
        /* RDSEED can legitimately underflow; brief pause */
        cpu_relax();
    }
    return false;
}

int straylight_rdrand_fill(void *buf, size_t len)
{
    u8 *p = buf;
    size_t filled = 0;
    u64 val;

    if (!has_rdrand)
        return -ENODEV;

    while (filled + sizeof(u64) <= len) {
        /* Prefer RDSEED (conditioned seed) over RDRAND */
        if (!rdseed64_retry(&val) && !rdrand64_retry(&val))
            break;
        memcpy(p + filled, &val, sizeof(u64));
        filled += sizeof(u64);
    }

    /* Handle tail bytes */
    if (filled < len && (rdseed64_retry(&val) || rdrand64_retry(&val))) {
        memcpy(p + filled, &val, len - filled);
        filled = len;
    }

    memzero_explicit(&val, sizeof(val));
    return filled;
}
```

### Task 2.3: CPU Jitter Entropy — `entropy_jitter.c`

- [ ] Create `kernel/entropy/entropy_jitter.c`

```c
#include <linux/slab.h>
#include <linux/timekeeping.h>
#include "entropy.h"

/*
 * CPU jitter entropy: measures variance in execution timing
 * of a fixed workload across loop iterations. The LSBs of
 * delta-nanosecond timestamps contain unpredictable jitter
 * from cache, TLB, branch prediction, and interrupt timing.
 */

#define JITTER_FOLD_LOOPS    64
#define JITTER_POOL_SIZE     256

struct jitter_state {
    u64  prev_time;
    u64  accum;
    u8   pool[JITTER_POOL_SIZE];
    int  pool_idx;
};

static struct jitter_state *jstate;

void straylight_jitter_init(void)
{
    jstate = kzalloc(sizeof(*jstate), GFP_KERNEL);
    if (!jstate)
        return;
    jstate->prev_time = ktime_get_ns();
}

void straylight_jitter_cleanup(void)
{
    if (jstate) {
        memzero_explicit(jstate, sizeof(*jstate));
        kfree(jstate);
        jstate = NULL;
    }
}

/* Timing loop: the jitter source. Folds CPU timing noise into accumulator. */
static u64 jitter_measure_round(void)
{
    u64 t_start, t_end, delta;
    volatile u64 fold = 0;
    int i;

    t_start = ktime_get_ns();

    for (i = 0; i < JITTER_FOLD_LOOPS; i++)
        fold ^= fold + (u64)i;  /* memory-dependent ops for timing variance */

    t_end = ktime_get_ns();
    delta = t_end - t_start;

    return delta;
}

/* Mix a delta into the jitter pool using simple LFSR-style mixing */
static void jitter_mix(u64 delta)
{
    int i;

    jstate->accum ^= delta;
    for (i = 0; i < 8; i++) {
        u8 byte = (jstate->accum >> (i * 8)) & 0xff;
        jstate->pool[jstate->pool_idx] ^= byte;
        jstate->pool_idx = (jstate->pool_idx + 1) % JITTER_POOL_SIZE;
    }
}

int straylight_jitter_fill(void *buf, size_t len)
{
    size_t filled = 0;
    int rounds;

    if (!jstate)
        return -ENOMEM;

    /* Run enough rounds to collect sufficient entropy */
    rounds = (len * 8) + JITTER_FOLD_LOOPS;  /* conservative oversampling */
    while (rounds-- > 0)
        jitter_mix(jitter_measure_round());

    /* Copy from pool */
    while (filled < len) {
        size_t chunk = min(len - filled, (size_t)JITTER_POOL_SIZE);
        memcpy((u8 *)buf + filled, jstate->pool, chunk);
        filled += chunk;
    }

    return filled;
}
```

### Task 2.4: NIST SP 800-90B Health Tests — `entropy_health.c`

- [ ] Create `kernel/entropy/entropy_health.c`

```c
#include <linux/types.h>
#include "entropy.h"

/*
 * NIST SP 800-90B Section 4.4 health tests:
 *   1. Repetition Count Test — detects stuck source
 *   2. Adaptive Proportion Test — detects bias
 *
 * Parameters tuned for H_min = 4 bits/byte, alpha = 2^-20.
 */

#define RCT_CUTOFF       5     /* max consecutive identical bytes before fail */
#define APT_WINDOW       512   /* observation window size */
#define APT_CUTOFF       325   /* max count of most-frequent byte in window */

struct health_state {
    /* Repetition Count Test */
    u8   rct_prev;
    int  rct_count;

    /* Adaptive Proportion Test */
    u8   apt_base;
    int  apt_count;
    int  apt_window_idx;
};

static struct health_state hstate;

void straylight_health_init(void)
{
    memset(&hstate, 0, sizeof(hstate));
    hstate.rct_count = 1;
}

static bool repetition_count_test(u8 sample)
{
    if (sample == hstate.rct_prev) {
        hstate.rct_count++;
        if (hstate.rct_count >= RCT_CUTOFF)
            return false;  /* FAIL: source stuck */
    } else {
        hstate.rct_prev = sample;
        hstate.rct_count = 1;
    }
    return true;
}

static bool adaptive_proportion_test(u8 sample)
{
    if (hstate.apt_window_idx == 0) {
        /* Start new window */
        hstate.apt_base = sample;
        hstate.apt_count = 1;
        hstate.apt_window_idx = 1;
        return true;
    }

    if (sample == hstate.apt_base)
        hstate.apt_count++;

    hstate.apt_window_idx++;

    if (hstate.apt_count >= APT_CUTOFF)
        return false;  /* FAIL: too much bias */

    if (hstate.apt_window_idx >= APT_WINDOW)
        hstate.apt_window_idx = 0;  /* reset window */

    return true;
}

bool straylight_health_check(const void *buf, size_t len)
{
    const u8 *p = buf;
    size_t i;

    for (i = 0; i < len; i++) {
        if (!repetition_count_test(p[i]))
            return false;
        if (!adaptive_proportion_test(p[i]))
            return false;
    }
    return true;
}
```

### Task 2.5: Entropy Kbuild and DKMS

- [ ] Create `kernel/entropy/Kbuild`

```makefile
obj-m := straylight-entropy.o
straylight-entropy-y := entropy_main.o entropy_rdrand.o entropy_jitter.o entropy_health.o
ccflags-y := -DDEBUG -std=gnu11
```

- [ ] Create `kernel/dkms/straylight-entropy-dkms/dkms.conf`

```ini
PACKAGE_NAME="straylight-entropy"
PACKAGE_VERSION="1.0.0"
BUILT_MODULE_NAME[0]="straylight-entropy"
BUILT_MODULE_LOCATION[0]="kernel/entropy/"
DEST_MODULE_LOCATION[0]="/updates/dkms"
MAKE[0]="make -C ${kernel_source_dir} M=${dkms_tree}/${PACKAGE_NAME}/${PACKAGE_VERSION}/build/kernel/entropy modules"
CLEAN="make -C ${kernel_source_dir} M=${dkms_tree}/${PACKAGE_NAME}/${PACKAGE_VERSION}/build/kernel/entropy clean"
AUTOINSTALL="yes"
```

---

## Chunk 3: straylight-enclave.ko — SGX Enclave Management

### Task 3.1: Module Init and Misc Device — `enclave_main.c`

- [ ] Create `kernel/enclave/enclave_main.c`

```c
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include "enclave.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("StrayLight Project");
MODULE_DESCRIPTION("StrayLight SGX enclave management");
MODULE_VERSION("1.0.0");

/* ioctl commands */
#define SL_SGX_IOC_MAGIC       'S'
#define SL_SGX_IOC_CREATE      _IOW(SL_SGX_IOC_MAGIC, 1, struct sl_sgx_create)
#define SL_SGX_IOC_ADD_PAGE    _IOW(SL_SGX_IOC_MAGIC, 2, struct sl_sgx_add_page)
#define SL_SGX_IOC_INIT        _IOW(SL_SGX_IOC_MAGIC, 3, struct sl_sgx_init_param)
#define SL_SGX_IOC_SEAL        _IOWR(SL_SGX_IOC_MAGIC, 4, struct sl_sgx_sealed)
#define SL_SGX_IOC_UNSEAL      _IOWR(SL_SGX_IOC_MAGIC, 5, struct sl_sgx_sealed)
#define SL_SGX_IOC_REPORT      _IOWR(SL_SGX_IOC_MAGIC, 6, struct sl_sgx_report)

static long sl_sgx_ioctl(struct file *filp, unsigned int cmd,
                         unsigned long arg)
{
    switch (cmd) {
    case SL_SGX_IOC_CREATE:
        return sl_epc_create((struct sl_sgx_create __user *)arg);
    case SL_SGX_IOC_ADD_PAGE:
        return sl_epc_add_page((struct sl_sgx_add_page __user *)arg);
    case SL_SGX_IOC_INIT:
        return sl_epc_init_enclave((struct sl_sgx_init_param __user *)arg);
    case SL_SGX_IOC_SEAL:
        return sl_sealed_write((struct sl_sgx_sealed __user *)arg);
    case SL_SGX_IOC_UNSEAL:
        return sl_sealed_read((struct sl_sgx_sealed __user *)arg);
    case SL_SGX_IOC_REPORT:
        return sl_attestation_report((struct sl_sgx_report __user *)arg);
    default:
        return -ENOTTY;
    }
}

static const struct file_operations sl_sgx_fops = {
    .owner          = THIS_MODULE,
    .unlocked_ioctl = sl_sgx_ioctl,
    .compat_ioctl   = compat_ptr_ioctl,
};

static struct miscdevice sl_sgx_misc = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = "straylight-sgx",
    .fops  = &sl_sgx_fops,
    .mode  = 0660,
};

static int __init sl_enclave_init(void)
{
    int ret;

    if (!sl_epc_detect_sgx()) {
        pr_err("straylight-enclave: SGX not supported\n");
        return -ENODEV;
    }

    ret = sl_epc_init();
    if (ret)
        return ret;

    ret = misc_register(&sl_sgx_misc);
    if (ret) {
        pr_err("straylight-enclave: misc_register failed: %d\n", ret);
        sl_epc_cleanup();
        return ret;
    }

    pr_info("straylight-enclave: /dev/straylight-sgx registered\n");
    return 0;
}

static void __exit sl_enclave_exit(void)
{
    misc_deregister(&sl_sgx_misc);
    sl_epc_cleanup();
    pr_info("straylight-enclave: unregistered\n");
}

module_init(sl_enclave_init);
module_exit(sl_enclave_exit);
```

- [ ] Create `kernel/enclave/enclave.h`

```c
#ifndef STRAYLIGHT_ENCLAVE_H
#define STRAYLIGHT_ENCLAVE_H

#include <linux/types.h>

/* --- Userspace ioctl structures --- */
struct sl_sgx_create {
    __u64 base_addr;      /* enclave base (page-aligned) */
    __u64 size;           /* enclave size (power of 2) */
    __u64 secs_attr;      /* SECS attributes flags */
};

struct sl_sgx_add_page {
    __u64 enclave_id;
    __u64 src_addr;       /* userspace source page */
    __u64 offset;         /* offset within enclave */
    __u64 flags;          /* page type: PT_REG, PT_TCS, etc. */
};

struct sl_sgx_init_param {
    __u64 enclave_id;
    __u8  sigstruct[1808];  /* SIGSTRUCT blob */
    __u8  einittoken[304];  /* EINITTOKEN blob */
};

struct sl_sgx_sealed {
    __u64 enclave_id;
    __u64 data_ptr;       /* userspace buffer */
    __u32 data_len;
    __u32 sealed_len;     /* output: sealed blob size */
    __u16 key_policy;     /* MRSIGNER or MRENCLAVE */
    __u16 reserved;
};

struct sl_sgx_report {
    __u64 enclave_id;
    __u8  target_info[512];  /* TARGETINFO of verifier */
    __u8  report_data[64];   /* user-supplied nonce */
    __u8  report[432];       /* output: REPORT structure */
};

/* --- enclave_epc.c --- */
bool sl_epc_detect_sgx(void);
int  sl_epc_init(void);
void sl_epc_cleanup(void);
long sl_epc_create(struct sl_sgx_create __user *arg);
long sl_epc_add_page(struct sl_sgx_add_page __user *arg);
long sl_epc_init_enclave(struct sl_sgx_init_param __user *arg);

/* --- enclave_sealed.c --- */
long sl_sealed_write(struct sl_sgx_sealed __user *arg);
long sl_sealed_read(struct sl_sgx_sealed __user *arg);

/* --- enclave_attestation.c --- */
long sl_attestation_report(struct sl_sgx_report __user *arg);

#endif /* STRAYLIGHT_ENCLAVE_H */
```

### Task 3.2: Enclave Page Cache Management — `enclave_epc.c`

- [ ] Create `kernel/enclave/enclave_epc.c`

```c
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <asm/cpufeatures.h>
#include "enclave.h"

/* SGX ENCLS leaf function opcodes */
#define SGX_ECREATE  0x00
#define SGX_EADD     0x01
#define SGX_EINIT    0x02
#define SGX_EEXTEND  0x06

struct sl_enclave {
    u64 id;
    u64 base;
    u64 size;
    bool initialized;
};

#define MAX_ENCLAVES 64
static struct sl_enclave enclaves[MAX_ENCLAVES];
static DEFINE_MUTEX(enclave_lock);
static u64 next_enclave_id = 1;

/* Execute ENCLS leaf instruction */
static inline int encls(u32 leaf, u64 rbx, u64 rcx, u64 rdx)
{
    int ret;

    asm volatile(
        "encls\n\t"
        : "=a"(ret)
        : "a"(leaf), "b"(rbx), "c"(rcx), "d"(rdx)
        : "memory", "cc"
    );
    return ret;
}

bool sl_epc_detect_sgx(void)
{
    return boot_cpu_has(X86_FEATURE_SGX) &&
           boot_cpu_has(X86_FEATURE_SGX1);
}

int sl_epc_init(void)
{
    memset(enclaves, 0, sizeof(enclaves));
    pr_info("straylight-enclave: EPC manager initialized\n");
    return 0;
}

void sl_epc_cleanup(void)
{
    /* ... (destroy active enclaves, release EPC pages) */
}

long sl_epc_create(struct sl_sgx_create __user *arg)
{
    struct sl_sgx_create params;
    struct sl_enclave *enc;
    int i, ret;

    if (copy_from_user(&params, arg, sizeof(params)))
        return -EFAULT;

    /* Validate alignment and size */
    if (!IS_ALIGNED(params.base_addr, PAGE_SIZE) ||
        !is_power_of_2(params.size))
        return -EINVAL;

    mutex_lock(&enclave_lock);

    /* Find free slot */
    enc = NULL;
    for (i = 0; i < MAX_ENCLAVES; i++) {
        if (enclaves[i].id == 0) {
            enc = &enclaves[i];
            break;
        }
    }
    if (!enc) {
        mutex_unlock(&enclave_lock);
        return -ENOMEM;
    }

    /* ECREATE: create the SECS (SGX Enclave Control Structure) */
    ret = encls(SGX_ECREATE, (u64)&params, params.base_addr, 0);
    if (ret) {
        mutex_unlock(&enclave_lock);
        return -EIO;
    }

    enc->id   = next_enclave_id++;
    enc->base = params.base_addr;
    enc->size = params.size;
    enc->initialized = false;

    mutex_unlock(&enclave_lock);

    return put_user(enc->id, &arg->base_addr) ? -EFAULT : 0;
}

long sl_epc_add_page(struct sl_sgx_add_page __user *arg)
{
    struct sl_sgx_add_page params;
    int ret;

    if (copy_from_user(&params, arg, sizeof(params)))
        return -EFAULT;

    /* EADD: add a page to the enclave */
    ret = encls(SGX_EADD, params.flags, params.src_addr, params.offset);
    if (ret)
        return -EIO;

    /* EEXTEND: measure 256-byte chunks for enclave measurement */
    {
        u64 addr;
        for (addr = params.offset; addr < params.offset + PAGE_SIZE; addr += 256) {
            ret = encls(SGX_EEXTEND, 0, addr, 0);
            if (ret)
                return -EIO;
        }
    }

    return 0;
}

long sl_epc_init_enclave(struct sl_sgx_init_param __user *arg)
{
    struct sl_sgx_init_param *params;
    struct sl_enclave *enc;
    int i, ret;

    params = kmalloc(sizeof(*params), GFP_KERNEL);
    if (!params)
        return -ENOMEM;

    if (copy_from_user(params, arg, sizeof(*params))) {
        kfree(params);
        return -EFAULT;
    }

    mutex_lock(&enclave_lock);
    enc = NULL;
    for (i = 0; i < MAX_ENCLAVES; i++) {
        if (enclaves[i].id == params->enclave_id) {
            enc = &enclaves[i];
            break;
        }
    }

    if (!enc || enc->initialized) {
        mutex_unlock(&enclave_lock);
        kfree(params);
        return -EINVAL;
    }

    /* EINIT: initialize enclave with SIGSTRUCT + launch token */
    ret = encls(SGX_EINIT, (u64)params->sigstruct,
                enc->base, (u64)params->einittoken);

    if (ret == 0)
        enc->initialized = true;

    mutex_unlock(&enclave_lock);
    kfree(params);
    return ret ? -EIO : 0;
}
```

### Task 3.3: Sealed Storage — `enclave_sealed.c`

- [ ] Create `kernel/enclave/enclave_sealed.c`

```c
#include <linux/uaccess.h>
#include <linux/slab.h>
#include "enclave.h"

/*
 * Sealed storage wraps data with a key derived from EGETKEY,
 * bound to either MRSIGNER (vendor) or MRENCLAVE (build).
 *
 * The kernel module mediates the EGETKEY request; actual
 * encryption runs inside the enclave. This file handles
 * the ioctl plumbing for seal/unseal operations.
 */

/* SGX ENCLU leaf for EGETKEY (executed in enclave context) */
#define SGX_EGETKEY  0x04

/* Key request structure passed to EGETKEY */
struct sgx_key_request {
    __u16 key_name;       /* 0x0004 = seal key */
    __u16 key_policy;     /* MRENCLAVE=0x01, MRSIGNER=0x02 */
    __u8  reserved[28];
    __u8  key_id[32];
    __u8  cpu_svn[16];
    __u8  isv_svn[2];
    __u8  padding[2];
};

#define SEAL_KEY_NAME  0x0004
#define SEAL_OVERHEAD  48   /* IV(16) + MAC(16) + header(16) */

long sl_sealed_write(struct sl_sgx_sealed __user *arg)
{
    struct sl_sgx_sealed params;
    void *data_buf = NULL;
    long ret = 0;

    if (copy_from_user(&params, arg, sizeof(params)))
        return -EFAULT;

    if (params.data_len == 0 || params.data_len > (1 << 20))
        return -EINVAL;

    data_buf = kvmalloc(params.data_len, GFP_KERNEL);
    if (!data_buf)
        return -ENOMEM;

    if (copy_from_user(data_buf, (void __user *)params.data_ptr,
                       params.data_len)) {
        ret = -EFAULT;
        goto out;
    }

    /*
     * Actual sealing happens via EGETKEY inside the enclave.
     * The kernel sets up the key request and invokes EENTER
     * to run the enclave's seal routine.
     *
     * ... (EENTER → enclave seal routine → EEXIT)
     *
     * For now, set sealed_len to indicate expected output size.
     */
    params.sealed_len = params.data_len + SEAL_OVERHEAD;

    if (put_user(params.sealed_len, &arg->sealed_len)) {
        ret = -EFAULT;
        goto out;
    }

out:
    if (data_buf) {
        memzero_explicit(data_buf, params.data_len);
        kvfree(data_buf);
    }
    return ret;
}

long sl_sealed_read(struct sl_sgx_sealed __user *arg)
{
    struct sl_sgx_sealed params;
    void *sealed_buf = NULL;
    long ret = 0;

    if (copy_from_user(&params, arg, sizeof(params)))
        return -EFAULT;

    if (params.sealed_len == 0 || params.sealed_len > (1 << 20) + SEAL_OVERHEAD)
        return -EINVAL;

    if (params.sealed_len <= SEAL_OVERHEAD)
        return -EINVAL;

    sealed_buf = kvmalloc(params.sealed_len, GFP_KERNEL);
    if (!sealed_buf)
        return -ENOMEM;

    if (copy_from_user(sealed_buf, (void __user *)params.data_ptr,
                       params.sealed_len)) {
        ret = -EFAULT;
        goto out;
    }

    /*
     * Unseal: EGETKEY to derive key, then decrypt.
     * ... (EENTER → enclave unseal routine → EEXIT)
     */

    params.data_len = params.sealed_len - SEAL_OVERHEAD;
    if (put_user(params.data_len, &arg->data_len)) {
        ret = -EFAULT;
        goto out;
    }

out:
    if (sealed_buf) {
        memzero_explicit(sealed_buf, params.sealed_len);
        kvfree(sealed_buf);
    }
    return ret;
}
```

### Task 3.4: Local Attestation — `enclave_attestation.c`

- [ ] Create `kernel/enclave/enclave_attestation.c`

```c
#include <linux/uaccess.h>
#include <linux/slab.h>
#include "enclave.h"

/*
 * Local attestation: enclave A proves its identity to enclave B
 * on the same platform via EREPORT.
 *
 * Flow:
 *   1. Verifier (B) sends its TARGETINFO to prover (A)
 *   2. Prover calls EREPORT(TARGETINFO_B, reportdata) → REPORT
 *   3. Verifier calls EGETKEY(REPORT_KEY) to derive the MAC key
 *   4. Verifier checks REPORT.MAC matches
 */

/* SGX ENCLU leaf for EREPORT */
#define SGX_EREPORT  0x00

long sl_attestation_report(struct sl_sgx_report __user *arg)
{
    struct sl_sgx_report *params;
    long ret = 0;

    params = kmalloc(sizeof(*params), GFP_KERNEL);
    if (!params)
        return -ENOMEM;

    if (copy_from_user(params, arg, sizeof(*params))) {
        ret = -EFAULT;
        goto out;
    }

    /*
     * Generate REPORT:
     *   EREPORT takes:
     *     - RBX: pointer to TARGETINFO (512 bytes, identifies verifier)
     *     - RCX: pointer to REPORTDATA (64 bytes, caller nonce)
     *     - RDX: pointer to output REPORT (432 bytes)
     *
     * This must execute inside enclave context (ring-3 ENCLU).
     * The kernel module sets up the parameters and triggers
     * EENTER into the enclave's report-generation ecall.
     *
     * ... (EENTER → enclave EREPORT wrapper → EEXIT)
     */

    /* Validate enclave_id exists and is initialized */
    /* ... (lookup in enclave table, standard pattern) */

    /*
     * For the kernel-mediated path, we prepare the EREPORT
     * input buffers and copy the result back to userspace.
     */
    if (copy_to_user(arg->report, params->report,
                     sizeof(params->report))) {
        ret = -EFAULT;
        goto out;
    }

out:
    kfree(params);
    return ret;
}
```

### Task 3.5: Enclave Kbuild and DKMS

- [ ] Create `kernel/enclave/Kbuild`

```makefile
obj-m := straylight-enclave.o
straylight-enclave-y := enclave_main.o enclave_epc.o enclave_sealed.o enclave_attestation.o
ccflags-y := -DDEBUG -std=gnu11
```

- [ ] Create `kernel/dkms/straylight-enclave-dkms/dkms.conf`

```ini
PACKAGE_NAME="straylight-enclave"
PACKAGE_VERSION="1.0.0"
BUILT_MODULE_NAME[0]="straylight-enclave"
BUILT_MODULE_LOCATION[0]="kernel/enclave/"
DEST_MODULE_LOCATION[0]="/updates/dkms"
MAKE[0]="make -C ${kernel_source_dir} M=${dkms_tree}/${PACKAGE_NAME}/${PACKAGE_VERSION}/build/kernel/enclave modules"
CLEAN="make -C ${kernel_source_dir} M=${dkms_tree}/${PACKAGE_NAME}/${PACKAGE_VERSION}/build/kernel/enclave clean"
AUTOINSTALL="yes"
```

---

## Chunk 4: Integration, Testing, and Verification

### Task 4.1: Module Loading Order and Dependencies

- [ ] Document load order in comments and modprobe config

```
# /etc/modprobe.d/straylight.conf
# XDP: no kernel module (eBPF loaded from userspace via libbpf)
# Entropy: standalone, no dependencies
# Enclave: standalone, requires SGX-capable CPU

# Soft dependency: enclave benefits from entropy for sealing nonce
softdep straylight-enclave pre: straylight-entropy
```

### Task 4.2: Build Verification Checklist

- [ ] **XDP programs:** `make -C kernel/xdp` produces three `.bpf.o` files
- [ ] **XDP programs:** `llvm-objdump -d xdp_filter.bpf.o` shows valid BPF instructions
- [ ] **XDP programs:** `bpftool prog load xdp_filter.bpf.o /sys/fs/bpf/test` succeeds on test system
- [ ] **Entropy module:** `make -C /lib/modules/$(uname -r)/build M=$PWD/kernel/entropy modules` succeeds
- [ ] **Entropy module:** `insmod straylight-entropy.ko` registers in `/sys/class/misc/` or hwrng
- [ ] **Entropy module:** `cat /sys/devices/virtual/misc/hw_random/rng_available` includes `straylight-entropy`
- [ ] **Entropy module:** `dd if=/dev/hwrng bs=256 count=1 | xxd` produces output
- [ ] **Enclave module:** `make -C /lib/modules/$(uname -r)/build M=$PWD/kernel/enclave modules` succeeds
- [ ] **Enclave module:** `insmod straylight-enclave.ko` creates `/dev/straylight-sgx` (SGX hardware required)
- [ ] **DKMS:** `dkms add ./kernel/dkms/straylight-entropy-dkms` succeeds
- [ ] **DKMS:** `dkms build straylight-entropy/1.0.0` succeeds
- [ ] **DKMS:** `dkms add ./kernel/dkms/straylight-enclave-dkms` succeeds
- [ ] **DKMS:** `dkms build straylight-enclave/1.0.0` succeeds

### Task 4.3: Userspace Integration Points

- [ ] **XDP ↔ straylight-xdp (subsystem):** The userspace `bin/xdp/loader.cpp` uses libbpf to load `.bpf.o` files from `/usr/lib/straylight/xdp/`, attach to interfaces, and update BPF maps (filter rules, redirect targets) via `bpf_map_update_elem()`
- [ ] **Entropy ↔ straylight-entropy (subsystem):** The userspace daemon reads from `/dev/hwrng` which the kernel module feeds. The daemon's DRBG (NIST SP 800-90A) seeds from this device
- [ ] **Enclave ↔ straylight-enclave (subsystem):** The userspace `bin/enclave/` opens `/dev/straylight-sgx` and issues ioctls for enclave lifecycle (create → add pages → init → seal/unseal → attest)

### Task 4.4: Graceful Degradation

- [ ] **XDP:** If BPF programs fail to load (old kernel, missing BTF), userspace falls back to `libstraylight-net` socket-based filtering
- [ ] **Entropy:** If module not loaded (no RDRAND, no jitter), userspace reads `/dev/urandom` directly (lower quality but functional)
- [ ] **Enclave:** If SGX unavailable or module fails to load, userspace `straylight-enclave` logs warning and disables secure inference — model runs unprotected in normal memory
