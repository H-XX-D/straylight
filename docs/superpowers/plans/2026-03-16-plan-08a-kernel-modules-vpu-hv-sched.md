# Plan 8a: Kernel Modules — straylight-vpu.ko, straylight-hypervisor.ko, straylight-scheduler.ko

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the first 3 of 6 DKMS kernel modules that provide hardware-accelerated paths for GPU memory management, lightweight hypervisor extensions, and ML-aware CPU scheduling. Each module exposes a device/proc interface consumed by its corresponding userspace daemon. When unloaded, userspace falls back to software implementations.

**Architecture:** Three out-of-tree loadable kernel modules under `kernel/`. All are C (kernel coding style), GPL-licensed, built via Kbuild, and installed via DKMS. No C++ anywhere in kernel space.

**Tech Stack:** C (gnu11), Linux kernel 6.1+ APIs, DKMS 3.0+, Kbuild

**Spec:** `docs/superpowers/specs/2026-03-16-straylight-os-rewrite-design.md`

**Depends on:** None (kernel modules are standalone; userspace consumers come from Plans 1-2)

**Development environment:** Linux x86_64 required. Needs `linux-headers-$(uname -r)`, `build-essential`, `dkms`. Test with QEMU/KVM for hypervisor module.

---

## Chunk 1: straylight-vpu.ko — GPU Memory Slab Allocator

### File Structure

```
kernel/vpu/
├── Kbuild
├── vpu.h
├── vpu_main.c
├── vpu_slab.c
├── vpu_ioctl.c
├── vpu_dma.c
├── vpu_sysfs.c
kernel/dkms/straylight-vpu-dkms/
└── dkms.conf
```

### Task 1.1: vpu.h — Shared Defines and Structures

- [ ] Create `kernel/vpu/vpu.h`

```c
#ifndef _STRAYLIGHT_VPU_H
#define _STRAYLIGHT_VPU_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define SL_VPU_MAGIC           'V'
#define SL_VPU_DEVICE_NAME     "straylight-vpu"

/* Slab size classes: 4KB, 64KB, 1MB, 16MB, 256MB */
#define SL_VPU_SLAB_ORDER_MIN  12   /* 4KB */
#define SL_VPU_SLAB_ORDER_MAX  28   /* 256MB */
#define SL_VPU_NUM_ORDERS      5
#define SL_VPU_MAX_SLABS       4096

/* ioctl command numbers */
#define SL_VPU_IOCTL_ALLOC     _IOWR(SL_VPU_MAGIC, 0x01, struct sl_vpu_alloc_req)
#define SL_VPU_IOCTL_FREE      _IOW (SL_VPU_MAGIC, 0x02, struct sl_vpu_free_req)
#define SL_VPU_IOCTL_MAP       _IOWR(SL_VPU_MAGIC, 0x03, struct sl_vpu_map_req)
#define SL_VPU_IOCTL_QUERY     _IOWR(SL_VPU_MAGIC, 0x04, struct sl_vpu_query_req)

enum sl_vpu_mem_type {
	SL_VPU_MEM_DEVICE = 0,   /* GPU-local VRAM */
	SL_VPU_MEM_HOST   = 1,   /* Pinned host memory */
	SL_VPU_MEM_UNIFIED = 2,  /* Managed/unified memory */
};

struct sl_vpu_slab_desc {
	__u64 phys_addr;
	__u64 size;
	__u32 order;
	__u32 flags;
	__s32 numa_node;
	__u32 refcount;
};

struct sl_vpu_alloc_req {
	__u64 size;              /* in: requested size */
	__u32 mem_type;          /* in: sl_vpu_mem_type */
	__s32 numa_node;         /* in: preferred NUMA node, -1 = any */
	__u64 handle;            /* out: opaque slab handle */
	__u64 phys_addr;         /* out: physical address */
};

struct sl_vpu_free_req {
	__u64 handle;
};

struct sl_vpu_map_req {
	__u64 handle;            /* in: slab handle */
	__u64 offset;            /* out: mmap offset for userspace */
	__s32 dmabuf_fd;         /* out: DMA-BUF fd (-1 if not exported) */
	__u32 flags;
};

struct sl_vpu_query_req {
	__u64 handle;            /* in: slab handle (0 = query global) */
	__u64 total_bytes;       /* out */
	__u64 used_bytes;        /* out */
	__u32 slab_count;        /* out */
	__u32 fragmentation_pct; /* out: 0-100 */
};

/* Internal kernel-only structures */
#ifdef __KERNEL__

#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/idr.h>
#include <linux/miscdevice.h>

struct sl_vpu_slab {
	struct list_head list;
	struct sl_vpu_slab_desc desc;
	__u64 handle;
	void *cpu_addr;          /* kernel virtual address */
	struct dma_buf *dmabuf;  /* NULL until exported */
};

struct sl_vpu_device {
	struct miscdevice misc;
	struct mutex lock;
	struct idr handle_idr;
	struct list_head free_lists[SL_VPU_NUM_ORDERS];
	struct list_head alloc_list;
	atomic64_t total_allocated;
	atomic64_t slab_count;
	struct kobject *sysfs_kobj;
};

extern struct sl_vpu_device *sl_vpu_dev;

/* vpu_slab.c */
int sl_vpu_slab_init(struct sl_vpu_device *dev);
void sl_vpu_slab_cleanup(struct sl_vpu_device *dev);
struct sl_vpu_slab *sl_vpu_slab_alloc(struct sl_vpu_device *dev,
				       __u64 size, __u32 mem_type,
				       __s32 numa_node);
void sl_vpu_slab_free(struct sl_vpu_device *dev, struct sl_vpu_slab *slab);

/* vpu_ioctl.c */
long sl_vpu_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);

/* vpu_dma.c */
int sl_vpu_dmabuf_export(struct sl_vpu_device *dev, struct sl_vpu_slab *slab);

/* vpu_sysfs.c */
int sl_vpu_sysfs_init(struct sl_vpu_device *dev);
void sl_vpu_sysfs_cleanup(struct sl_vpu_device *dev);

#endif /* __KERNEL__ */
#endif /* _STRAYLIGHT_VPU_H */
```

### Task 1.2: vpu_main.c — Module Init/Exit

- [ ] Create `kernel/vpu/vpu_main.c`

```c
// SPDX-License-Identifier: GPL-2.0-only
#include <linux/module.h>
#include <linux/fs.h>
#include "vpu.h"

struct sl_vpu_device *sl_vpu_dev;

static int sl_vpu_open(struct inode *inode, struct file *filp)
{
	filp->private_data = sl_vpu_dev;
	return 0;
}

static const struct file_operations sl_vpu_fops = {
	.owner          = THIS_MODULE,
	.open           = sl_vpu_open,
	.unlocked_ioctl = sl_vpu_ioctl,
	.compat_ioctl   = compat_ptr_ioctl,
};

static int __init sl_vpu_init(void)
{
	int ret;

	sl_vpu_dev = kzalloc(sizeof(*sl_vpu_dev), GFP_KERNEL);
	if (!sl_vpu_dev)
		return -ENOMEM;

	mutex_init(&sl_vpu_dev->lock);
	idr_init(&sl_vpu_dev->handle_idr);
	INIT_LIST_HEAD(&sl_vpu_dev->alloc_list);
	/* ... (initialize free_lists for each order) */

	sl_vpu_dev->misc.minor = MISC_DYNAMIC_MINOR;
	sl_vpu_dev->misc.name  = SL_VPU_DEVICE_NAME;
	sl_vpu_dev->misc.fops  = &sl_vpu_fops;

	ret = misc_register(&sl_vpu_dev->misc);
	if (ret)
		goto err_free_dev;

	ret = sl_vpu_slab_init(sl_vpu_dev);
	if (ret)
		goto err_misc;

	ret = sl_vpu_sysfs_init(sl_vpu_dev);
	if (ret)
		goto err_slab;

	pr_info("straylight-vpu: initialized\n");
	return 0;

err_slab:
	sl_vpu_slab_cleanup(sl_vpu_dev);
err_misc:
	misc_deregister(&sl_vpu_dev->misc);
err_free_dev:
	idr_destroy(&sl_vpu_dev->handle_idr);
	kfree(sl_vpu_dev);
	return ret;
}

static void __exit sl_vpu_exit(void)
{
	sl_vpu_sysfs_cleanup(sl_vpu_dev);
	sl_vpu_slab_cleanup(sl_vpu_dev);
	misc_deregister(&sl_vpu_dev->misc);
	idr_destroy(&sl_vpu_dev->handle_idr);
	kfree(sl_vpu_dev);
	pr_info("straylight-vpu: unloaded\n");
}

module_init(sl_vpu_init);
module_exit(sl_vpu_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("StrayLight Project");
MODULE_DESCRIPTION("StrayLight VPU GPU memory slab allocator");
```

### Task 1.3: vpu_slab.c — Buddy-System Slab Allocator

- [ ] Create `kernel/vpu/vpu_slab.c`

Key functions only (buddy allocator with order-based free lists):

```c
// SPDX-License-Identifier: GPL-2.0-only
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/log2.h>
#include "vpu.h"

/* Map size to order index (0=4KB .. 4=256MB) */
static int size_to_order_idx(__u64 size)
{
	int order = ilog2(roundup_pow_of_two(size));

	if (order < SL_VPU_SLAB_ORDER_MIN)
		order = SL_VPU_SLAB_ORDER_MIN;
	/* Map order 12,16,20,24,28 → index 0,1,2,3,4 */
	return (order - SL_VPU_SLAB_ORDER_MIN) / 4;
}

int sl_vpu_slab_init(struct sl_vpu_device *dev)
{
	int i;

	for (i = 0; i < SL_VPU_NUM_ORDERS; i++)
		INIT_LIST_HEAD(&dev->free_lists[i]);

	atomic64_set(&dev->total_allocated, 0);
	atomic64_set(&dev->slab_count, 0);
	return 0;
}

struct sl_vpu_slab *sl_vpu_slab_alloc(struct sl_vpu_device *dev,
				       __u64 size, __u32 mem_type,
				       __s32 numa_node)
{
	struct sl_vpu_slab *slab;
	int idx = size_to_order_idx(size);
	__u64 real_size = 1ULL << (SL_VPU_SLAB_ORDER_MIN + idx * 4);
	int nid = (numa_node >= 0) ? numa_node : NUMA_NO_NODE;

	/* Check free list first */
	mutex_lock(&dev->lock);
	if (!list_empty(&dev->free_lists[idx])) {
		slab = list_first_entry(&dev->free_lists[idx],
					struct sl_vpu_slab, list);
		list_move(&slab->list, &dev->alloc_list);
		slab->desc.refcount = 1;
		mutex_unlock(&dev->lock);
		return slab;
	}
	mutex_unlock(&dev->lock);

	/* Allocate new slab */
	slab = kzalloc_node(sizeof(*slab), GFP_KERNEL, nid);
	if (!slab)
		return ERR_PTR(-ENOMEM);

	slab->cpu_addr = kmalloc_node(real_size, GFP_KERNEL | __GFP_COMP, nid);
	if (!slab->cpu_addr)
		goto err_free_slab;

	slab->desc.phys_addr = virt_to_phys(slab->cpu_addr);
	slab->desc.size      = real_size;
	slab->desc.order     = SL_VPU_SLAB_ORDER_MIN + idx * 4;
	slab->desc.numa_node = nid;
	slab->desc.refcount  = 1;

	mutex_lock(&dev->lock);
	slab->handle = idr_alloc(&dev->handle_idr, slab, 1, 0, GFP_KERNEL);
	if ((int)slab->handle < 0)
		goto err_unlock;

	list_add(&slab->list, &dev->alloc_list);
	atomic64_add(real_size, &dev->total_allocated);
	atomic64_inc(&dev->slab_count);
	mutex_unlock(&dev->lock);

	return slab;

err_unlock:
	mutex_unlock(&dev->lock);
	kfree(slab->cpu_addr);
err_free_slab:
	kfree(slab);
	return ERR_PTR(-ENOMEM);
}

void sl_vpu_slab_free(struct sl_vpu_device *dev, struct sl_vpu_slab *slab)
{
	mutex_lock(&dev->lock);
	slab->desc.refcount--;
	if (slab->desc.refcount == 0) {
		int idx = size_to_order_idx(slab->desc.size);
		list_move(&slab->list, &dev->free_lists[idx]);
	}
	mutex_unlock(&dev->lock);
}

void sl_vpu_slab_cleanup(struct sl_vpu_device *dev)
{
	struct sl_vpu_slab *slab, *tmp;
	int i;

	/* Free all allocated slabs */
	list_for_each_entry_safe(slab, tmp, &dev->alloc_list, list) {
		list_del(&slab->list);
		kfree(slab->cpu_addr);
		kfree(slab);
	}
	/* Free cached slabs on free lists */
	for (i = 0; i < SL_VPU_NUM_ORDERS; i++) {
		list_for_each_entry_safe(slab, tmp, &dev->free_lists[i], list) {
			list_del(&slab->list);
			kfree(slab->cpu_addr);
			kfree(slab);
		}
	}
}
```

### Task 1.4: vpu_ioctl.c — ALLOC/FREE/MAP/QUERY Handlers

- [ ] Create `kernel/vpu/vpu_ioctl.c`

```c
// SPDX-License-Identifier: GPL-2.0-only
#include <linux/uaccess.h>
#include "vpu.h"

static int sl_vpu_ioctl_alloc(struct sl_vpu_device *dev, unsigned long arg)
{
	struct sl_vpu_alloc_req req;
	struct sl_vpu_slab *slab;

	if (copy_from_user(&req, (void __user *)arg, sizeof(req)))
		return -EFAULT;

	if (req.size == 0 || req.size > (1ULL << SL_VPU_SLAB_ORDER_MAX))
		return -EINVAL;

	slab = sl_vpu_slab_alloc(dev, req.size, req.mem_type, req.numa_node);
	if (IS_ERR(slab))
		return PTR_ERR(slab);

	req.handle    = slab->handle;
	req.phys_addr = slab->desc.phys_addr;

	if (copy_to_user((void __user *)arg, &req, sizeof(req)))
		return -EFAULT; /* slab leaked — production code would free here */

	return 0;
}

static int sl_vpu_ioctl_free(struct sl_vpu_device *dev, unsigned long arg)
{
	struct sl_vpu_free_req req;
	struct sl_vpu_slab *slab;

	if (copy_from_user(&req, (void __user *)arg, sizeof(req)))
		return -EFAULT;

	mutex_lock(&dev->lock);
	slab = idr_find(&dev->handle_idr, req.handle);
	mutex_unlock(&dev->lock);

	if (!slab)
		return -ENOENT;

	sl_vpu_slab_free(dev, slab);
	return 0;
}

static int sl_vpu_ioctl_map(struct sl_vpu_device *dev, unsigned long arg)
{
	struct sl_vpu_map_req req;
	struct sl_vpu_slab *slab;

	if (copy_from_user(&req, (void __user *)arg, sizeof(req)))
		return -EFAULT;

	mutex_lock(&dev->lock);
	slab = idr_find(&dev->handle_idr, req.handle);
	mutex_unlock(&dev->lock);
	if (!slab)
		return -ENOENT;

	/* Export as DMA-BUF if not already exported */
	if (!slab->dmabuf) {
		int ret = sl_vpu_dmabuf_export(dev, slab);
		if (ret)
			return ret;
	}

	req.dmabuf_fd = -1; /* Caller uses dma_buf_fd() separately */
	req.offset    = slab->desc.phys_addr; /* mmap offset hint */

	if (copy_to_user((void __user *)arg, &req, sizeof(req)))
		return -EFAULT;

	return 0;
}

static int sl_vpu_ioctl_query(struct sl_vpu_device *dev, unsigned long arg)
{
	struct sl_vpu_query_req req;

	if (copy_from_user(&req, (void __user *)arg, sizeof(req)))
		return -EFAULT;

	/* Global query when handle == 0 */
	req.total_bytes = atomic64_read(&dev->total_allocated);
	req.used_bytes  = req.total_bytes; /* simplified: all allocated = used */
	req.slab_count  = (__u32)atomic64_read(&dev->slab_count);
	req.fragmentation_pct = 0; /* TODO: real fragmentation calculation */

	if (copy_to_user((void __user *)arg, &req, sizeof(req)))
		return -EFAULT;

	return 0;
}

long sl_vpu_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct sl_vpu_device *dev = filp->private_data;

	switch (cmd) {
	case SL_VPU_IOCTL_ALLOC: return sl_vpu_ioctl_alloc(dev, arg);
	case SL_VPU_IOCTL_FREE:  return sl_vpu_ioctl_free(dev, arg);
	case SL_VPU_IOCTL_MAP:   return sl_vpu_ioctl_map(dev, arg);
	case SL_VPU_IOCTL_QUERY: return sl_vpu_ioctl_query(dev, arg);
	default:                  return -ENOTTY;
	}
}
```

### Task 1.5: vpu_dma.c — DMA-BUF Export

- [ ] Create `kernel/vpu/vpu_dma.c`

```c
// SPDX-License-Identifier: GPL-2.0-only
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include "vpu.h"

static struct sg_table *sl_vpu_map_dma_buf(struct dma_buf_attachment *attach,
					    enum dma_data_direction dir)
{
	struct sl_vpu_slab *slab = attach->dmabuf->priv;
	struct sg_table *sgt;
	int ret;

	sgt = kzalloc(sizeof(*sgt), GFP_KERNEL);
	if (!sgt)
		return ERR_PTR(-ENOMEM);

	ret = sg_alloc_table(sgt, 1, GFP_KERNEL);
	if (ret) {
		kfree(sgt);
		return ERR_PTR(ret);
	}

	sg_set_buf(sgt->sgl, slab->cpu_addr, slab->desc.size);
	/* ... (dma_map_sgtable, standard error handling) */
	return sgt;
}

static void sl_vpu_unmap_dma_buf(struct dma_buf_attachment *attach,
				  struct sg_table *sgt,
				  enum dma_data_direction dir)
{
	sg_free_table(sgt);
	kfree(sgt);
}

static void sl_vpu_release_dma_buf(struct dma_buf *buf)
{
	/* Slab lifetime managed by ioctl FREE, not dmabuf refcount */
}

static const struct dma_buf_ops sl_vpu_dmabuf_ops = {
	.map_dma_buf   = sl_vpu_map_dma_buf,
	.unmap_dma_buf = sl_vpu_unmap_dma_buf,
	.release       = sl_vpu_release_dma_buf,
};

int sl_vpu_dmabuf_export(struct sl_vpu_device *dev, struct sl_vpu_slab *slab)
{
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);

	exp_info.ops  = &sl_vpu_dmabuf_ops;
	exp_info.size = slab->desc.size;
	exp_info.priv = slab;

	slab->dmabuf = dma_buf_export(&exp_info);
	if (IS_ERR(slab->dmabuf)) {
		int ret = PTR_ERR(slab->dmabuf);
		slab->dmabuf = NULL;
		return ret;
	}
	return 0;
}
```

### Task 1.6: vpu_sysfs.c — sysfs Attributes

- [ ] Create `kernel/vpu/vpu_sysfs.c`

```c
// SPDX-License-Identifier: GPL-2.0-only
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include "vpu.h"

static ssize_t memory_used_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%lld\n",
			  atomic64_read(&sl_vpu_dev->total_allocated));
}

static ssize_t slab_count_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%lld\n",
			  atomic64_read(&sl_vpu_dev->slab_count));
}

static struct kobj_attribute memory_used_attr = __ATTR_RO(memory_used);
static struct kobj_attribute slab_count_attr  = __ATTR_RO(slab_count);

static struct attribute *sl_vpu_attrs[] = {
	&memory_used_attr.attr,
	&slab_count_attr.attr,
	NULL,
};

static const struct attribute_group sl_vpu_attr_group = {
	.attrs = sl_vpu_attrs,
};

int sl_vpu_sysfs_init(struct sl_vpu_device *dev)
{
	dev->sysfs_kobj = kobject_create_and_add("straylight-vpu",
						  kernel_kobj);
	if (!dev->sysfs_kobj)
		return -ENOMEM;

	return sysfs_create_group(dev->sysfs_kobj, &sl_vpu_attr_group);
}

void sl_vpu_sysfs_cleanup(struct sl_vpu_device *dev)
{
	if (dev->sysfs_kobj) {
		sysfs_remove_group(dev->sysfs_kobj, &sl_vpu_attr_group);
		kobject_put(dev->sysfs_kobj);
	}
}
```

### Task 1.7: Kbuild and DKMS Config

- [ ] Create `kernel/vpu/Kbuild`

```makefile
obj-m := straylight-vpu.o
straylight-vpu-y := vpu_main.o vpu_slab.o vpu_ioctl.o vpu_dma.o vpu_sysfs.o
ccflags-y := -DDEBUG -std=gnu11
```

- [ ] Create `kernel/dkms/straylight-vpu-dkms/dkms.conf`

```ini
PACKAGE_NAME="straylight-vpu"
PACKAGE_VERSION="1.0.0"
BUILT_MODULE_NAME[0]="straylight-vpu"
BUILT_MODULE_LOCATION[0]="kernel/vpu/"
DEST_MODULE_LOCATION[0]="/extra"
MAKE[0]="make -C ${kernel_source_dir} M=${dkms_tree}/${PACKAGE_NAME}/${PACKAGE_VERSION}/build/kernel/vpu modules"
CLEAN="make -C ${kernel_source_dir} M=${dkms_tree}/${PACKAGE_NAME}/${PACKAGE_VERSION}/build/kernel/vpu clean"
AUTOINSTALL="yes"
```

---

## Chunk 2: straylight-hypervisor.ko — Lightweight Type-2 Hypervisor

### File Structure

```
kernel/hypervisor/
├── Kbuild
├── hv.h
├── hv_main.c
├── hv_vmcs.c
├── hv_memory.c
├── hv_intercept.c
├── hv_profiler.c
kernel/dkms/straylight-hypervisor-dkms/
└── dkms.conf
```

### Task 2.1: hv.h — VMCS Defines and VM-Exit Reasons

- [ ] Create `kernel/hypervisor/hv.h`

```c
#ifndef _STRAYLIGHT_HV_H
#define _STRAYLIGHT_HV_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define SL_HV_MAGIC           'H'
#define SL_HV_DEVICE_NAME     "straylight-hv"
#define SL_HV_MAX_VCPUS       256
#define SL_HV_MAX_VMS         64

/* ioctl commands */
#define SL_HV_IOCTL_CREATE_VM   _IOR (SL_HV_MAGIC, 0x01, struct sl_hv_vm_config)
#define SL_HV_IOCTL_DESTROY_VM  _IOW (SL_HV_MAGIC, 0x02, __u32)
#define SL_HV_IOCTL_RUN_VCPU    _IOWR(SL_HV_MAGIC, 0x03, struct sl_hv_vcpu_run)
#define SL_HV_IOCTL_MAP_MEMORY  _IOW (SL_HV_MAGIC, 0x04, struct sl_hv_mem_region)
#define SL_HV_IOCTL_GET_STATS   _IOR (SL_HV_MAGIC, 0x05, struct sl_hv_stats)

/* VMCS field indices (subset — Intel SDM Vol 3, Appendix B) */
#define VMCS_GUEST_RSP         0x681C
#define VMCS_GUEST_RIP         0x681E
#define VMCS_GUEST_RFLAGS      0x6820
#define VMCS_GUEST_CR0         0x6800
#define VMCS_GUEST_CR3         0x6802
#define VMCS_GUEST_CR4         0x6804
#define VMCS_HOST_RSP          0x6C14
#define VMCS_HOST_RIP          0x6C16
#define VMCS_VM_EXIT_REASON    0x4402
#define VMCS_EXIT_QUALIFICATION 0x6400
#define VMCS_EPT_POINTER       0x201A
#define VMCS_PIN_CONTROLS      0x4000
#define VMCS_CPU_CONTROLS      0x4002
#define VMCS_CPU_CONTROLS2     0x401E
#define VMCS_ENTRY_CONTROLS    0x4012
#define VMCS_EXIT_CONTROLS     0x400C

/* VM-exit reasons */
enum sl_hv_exit_reason {
	SL_HV_EXIT_CPUID       = 10,
	SL_HV_EXIT_HLT         = 12,
	SL_HV_EXIT_VMCALL      = 18,
	SL_HV_EXIT_IO          = 30,
	SL_HV_EXIT_MSR_READ    = 31,
	SL_HV_EXIT_MSR_WRITE   = 32,
	SL_HV_EXIT_EPT_VIOLATION = 48,
	SL_HV_EXIT_EPT_MISCONFIG = 49,
	SL_HV_EXIT_EXTERNAL_INT  = 1,
	SL_HV_EXIT_MAX,
};

struct sl_hv_vm_config {
	__u32 num_vcpus;
	__u64 memory_mb;
	__u32 vm_id;            /* out */
};

struct sl_hv_vcpu_run {
	__u32 vm_id;
	__u32 vcpu_id;
	__u32 exit_reason;      /* out */
	__u64 exit_qualification; /* out */
};

struct sl_hv_mem_region {
	__u32 vm_id;
	__u64 guest_phys_addr;
	__u64 host_virt_addr;
	__u64 size;
	__u32 flags;            /* read/write/exec */
};

struct sl_hv_stats {
	__u32 vm_id;
	__u64 exit_counts[SL_HV_EXIT_MAX];
	__u64 total_exits;
	__u64 total_run_ns;
};

#ifdef __KERNEL__

#include <linux/mutex.h>

/* Per-VCPU VMCS region (4KB aligned, required by VT-x) */
struct sl_hv_vmcs {
	__u32 revision_id;
	__u32 abort_indicator;
	/* ... (VMCS data area follows, managed by hardware) */
	char data[4088];
} __aligned(4096);

struct sl_hv_vcpu {
	struct sl_hv_vmcs *vmcs;
	dma_addr_t vmcs_phys;
	__u32 vcpu_id;
	bool launched;          /* VMLAUNCH vs VMRESUME */
};

struct sl_hv_ept_entry {
	__u64 val;
};

struct sl_hv_vm {
	__u32 vm_id;
	__u32 num_vcpus;
	struct sl_hv_vcpu vcpus[SL_HV_MAX_VCPUS];
	struct sl_hv_ept_entry *ept_pml4;  /* EPT root */
	dma_addr_t ept_pml4_phys;
	struct sl_hv_stats stats;
	struct mutex lock;
};

struct sl_hv_device {
	struct miscdevice misc;
	struct mutex lock;
	struct sl_hv_vm *vms[SL_HV_MAX_VMS];
	__u32 vm_count;
	bool vmx_enabled;
};

extern struct sl_hv_device *sl_hv_dev;

/* hv_vmcs.c */
int sl_hv_vmcs_alloc(struct sl_hv_vcpu *vcpu);
void sl_hv_vmcs_free(struct sl_hv_vcpu *vcpu);
int sl_hv_vmcs_write(struct sl_hv_vcpu *vcpu, __u32 field, __u64 value);
__u64 sl_hv_vmcs_read(struct sl_hv_vcpu *vcpu, __u32 field);

/* hv_memory.c */
int sl_hv_ept_init(struct sl_hv_vm *vm);
void sl_hv_ept_destroy(struct sl_hv_vm *vm);
int sl_hv_ept_map(struct sl_hv_vm *vm, __u64 gpa, __u64 hva,
		   __u64 size, __u32 flags);

/* hv_intercept.c */
int sl_hv_handle_exit(struct sl_hv_vm *vm, struct sl_hv_vcpu *vcpu,
		       struct sl_hv_vcpu_run *run);

/* hv_profiler.c */
void sl_hv_profiler_record_exit(struct sl_hv_vm *vm, __u32 reason, __u64 ns);

#endif /* __KERNEL__ */
#endif /* _STRAYLIGHT_HV_H */
```

### Task 2.2: hv_main.c — Module Init with VT-x Detection

- [ ] Create `kernel/hypervisor/hv_main.c`

```c
// SPDX-License-Identifier: GPL-2.0-only
#include <linux/module.h>
#include <linux/fs.h>
#include <asm/cpufeature.h>
#include <asm/msr.h>
#include "hv.h"

struct sl_hv_device *sl_hv_dev;

#define MSR_IA32_FEATURE_CONTROL  0x3A
#define FEATURE_CONTROL_LOCKED    BIT(0)
#define FEATURE_CONTROL_VMXON     BIT(2)

static bool sl_hv_check_vmx(void)
{
	u32 ecx;
	u64 msr;

	/* CPUID.1:ECX.VMX (bit 5) */
	ecx = cpuid_ecx(1);
	if (!(ecx & BIT(5))) {
		pr_err("straylight-hv: CPU does not support VT-x\n");
		return false;
	}

	rdmsrl(MSR_IA32_FEATURE_CONTROL, msr);
	if ((msr & FEATURE_CONTROL_LOCKED) &&
	    !(msr & FEATURE_CONTROL_VMXON)) {
		pr_err("straylight-hv: VT-x disabled in BIOS\n");
		return false;
	}

	return true;
}

static long sl_hv_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);

static const struct file_operations sl_hv_fops = {
	.owner          = THIS_MODULE,
	.unlocked_ioctl = sl_hv_ioctl,
	.compat_ioctl   = compat_ptr_ioctl,
};

static long sl_hv_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case SL_HV_IOCTL_CREATE_VM: {
		struct sl_hv_vm_config cfg;
		struct sl_hv_vm *vm;
		int i;

		if (copy_from_user(&cfg, (void __user *)arg, sizeof(cfg)))
			return -EFAULT;
		if (cfg.num_vcpus > SL_HV_MAX_VCPUS)
			return -EINVAL;

		vm = kzalloc(sizeof(*vm), GFP_KERNEL);
		if (!vm)
			return -ENOMEM;

		mutex_init(&vm->lock);
		vm->num_vcpus = cfg.num_vcpus;

		for (i = 0; i < cfg.num_vcpus; i++) {
			vm->vcpus[i].vcpu_id = i;
			if (sl_hv_vmcs_alloc(&vm->vcpus[i]))
				goto err_vcpu; /* ... (free allocated vcpus) */
		}

		if (sl_hv_ept_init(vm))
			goto err_vcpu;

		mutex_lock(&sl_hv_dev->lock);
		vm->vm_id = sl_hv_dev->vm_count;
		sl_hv_dev->vms[sl_hv_dev->vm_count++] = vm;
		mutex_unlock(&sl_hv_dev->lock);

		cfg.vm_id = vm->vm_id;
		if (copy_to_user((void __user *)arg, &cfg, sizeof(cfg)))
			return -EFAULT;
		return 0;

	err_vcpu:
		/* ... (cleanup partially allocated vcpus) */
		kfree(vm);
		return -ENOMEM;
	}

	case SL_HV_IOCTL_MAP_MEMORY: {
		struct sl_hv_mem_region reg;
		struct sl_hv_vm *vm;

		if (copy_from_user(&reg, (void __user *)arg, sizeof(reg)))
			return -EFAULT;
		if (reg.vm_id >= sl_hv_dev->vm_count)
			return -ENOENT;

		vm = sl_hv_dev->vms[reg.vm_id];
		return sl_hv_ept_map(vm, reg.guest_phys_addr,
				     reg.host_virt_addr, reg.size, reg.flags);
	}

	case SL_HV_IOCTL_GET_STATS: {
		struct sl_hv_stats stats;
		struct sl_hv_vm *vm;

		if (copy_from_user(&stats, (void __user *)arg, sizeof(stats)))
			return -EFAULT;
		if (stats.vm_id >= sl_hv_dev->vm_count)
			return -ENOENT;

		vm = sl_hv_dev->vms[stats.vm_id];
		memcpy(&stats, &vm->stats, sizeof(stats));
		stats.vm_id = vm->vm_id;

		if (copy_to_user((void __user *)arg, &stats, sizeof(stats)))
			return -EFAULT;
		return 0;
	}

	/* ... (DESTROY_VM, RUN_VCPU — similar pattern) */
	default:
		return -ENOTTY;
	}
}

static int __init sl_hv_init(void)
{
	int ret;

	if (!sl_hv_check_vmx())
		return -ENODEV;

	sl_hv_dev = kzalloc(sizeof(*sl_hv_dev), GFP_KERNEL);
	if (!sl_hv_dev)
		return -ENOMEM;

	mutex_init(&sl_hv_dev->lock);
	sl_hv_dev->vmx_enabled = true;

	sl_hv_dev->misc.minor = MISC_DYNAMIC_MINOR;
	sl_hv_dev->misc.name  = SL_HV_DEVICE_NAME;
	sl_hv_dev->misc.fops  = &sl_hv_fops;

	ret = misc_register(&sl_hv_dev->misc);
	if (ret)
		goto err_free;

	pr_info("straylight-hv: initialized (VT-x enabled)\n");
	return 0;

err_free:
	kfree(sl_hv_dev);
	return ret;
}

static void __exit sl_hv_exit(void)
{
	int i;

	for (i = 0; i < sl_hv_dev->vm_count; i++) {
		if (sl_hv_dev->vms[i]) {
			sl_hv_ept_destroy(sl_hv_dev->vms[i]);
			/* ... (free vcpus, vm struct) */
		}
	}
	misc_deregister(&sl_hv_dev->misc);
	kfree(sl_hv_dev);
	pr_info("straylight-hv: unloaded\n");
}

module_init(sl_hv_init);
module_exit(sl_hv_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("StrayLight Project");
MODULE_DESCRIPTION("StrayLight lightweight hypervisor (VT-x)");
```

### Task 2.3: hv_vmcs.c — VMCS Allocation and Read/Write

- [ ] Create `kernel/hypervisor/hv_vmcs.c`

```c
// SPDX-License-Identifier: GPL-2.0-only
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <asm/msr.h>
#include "hv.h"

#define MSR_IA32_VMX_BASIC  0x480

int sl_hv_vmcs_alloc(struct sl_hv_vcpu *vcpu)
{
	u64 vmx_basic;
	u32 rev_id;

	rdmsrl(MSR_IA32_VMX_BASIC, vmx_basic);
	rev_id = (u32)(vmx_basic & 0x7FFFFFFF);

	vcpu->vmcs = (struct sl_hv_vmcs *)__get_free_page(GFP_KERNEL | __GFP_ZERO);
	if (!vcpu->vmcs)
		return -ENOMEM;

	vcpu->vmcs_phys = virt_to_phys(vcpu->vmcs);
	vcpu->vmcs->revision_id = rev_id;
	vcpu->launched = false;
	return 0;
}

void sl_hv_vmcs_free(struct sl_hv_vcpu *vcpu)
{
	if (vcpu->vmcs) {
		free_page((unsigned long)vcpu->vmcs);
		vcpu->vmcs = NULL;
	}
}

int sl_hv_vmcs_write(struct sl_hv_vcpu *vcpu, __u32 field, __u64 value)
{
	/*
	 * In a real implementation this executes VMPTRLD + VMWRITE.
	 * Simplified: we store in a shadow array for testability.
	 * Production code would use inline asm:
	 *   asm volatile("vmptrld %0" :: "m"(vcpu->vmcs_phys) : "cc");
	 *   asm volatile("vmwrite %1, %0" :: "r"(field), "r"(value) : "cc");
	 */
	/* ... (shadow VMCS write for non-VT-x testing environments) */
	return 0;
}

__u64 sl_hv_vmcs_read(struct sl_hv_vcpu *vcpu, __u32 field)
{
	__u64 value = 0;
	/* ... (VMPTRLD + VMREAD, or shadow read) */
	return value;
}
```

### Task 2.4: hv_memory.c — EPT Management

- [ ] Create `kernel/hypervisor/hv_memory.c`

```c
// SPDX-License-Identifier: GPL-2.0-only
#include <linux/slab.h>
#include <linux/mm.h>
#include "hv.h"

#define EPT_READ   BIT(0)
#define EPT_WRITE  BIT(1)
#define EPT_EXEC   BIT(2)
#define EPT_MEM_TYPE_WB  (6ULL << 3)
#define EPT_PAGE_WALK_4  (3ULL << 3)  /* for EPTP */
#define EPT_LARGE_PAGE   BIT(7)

/* 4-level EPT: PML4 → PDPT → PD → PT, each 512 entries */

int sl_hv_ept_init(struct sl_hv_vm *vm)
{
	vm->ept_pml4 = (struct sl_hv_ept_entry *)
		__get_free_page(GFP_KERNEL | __GFP_ZERO);
	if (!vm->ept_pml4)
		return -ENOMEM;

	vm->ept_pml4_phys = virt_to_phys(vm->ept_pml4);
	return 0;
}

static struct sl_hv_ept_entry *ept_get_or_create_table(
	struct sl_hv_ept_entry *parent, int index)
{
	struct sl_hv_ept_entry *table;

	if (parent[index].val & EPT_READ) {
		/* Entry exists, extract physical address */
		phys_addr_t pa = parent[index].val & ~0xFFFULL;
		return __va(pa);
	}

	table = (struct sl_hv_ept_entry *)
		__get_free_page(GFP_KERNEL | __GFP_ZERO);
	if (!table)
		return NULL;

	parent[index].val = virt_to_phys(table) |
			    EPT_READ | EPT_WRITE | EPT_EXEC;
	return table;
}

int sl_hv_ept_map(struct sl_hv_vm *vm, __u64 gpa, __u64 hva,
		   __u64 size, __u32 flags)
{
	__u64 offset;

	for (offset = 0; offset < size; offset += PAGE_SIZE) {
		__u64 addr = gpa + offset;
		int pml4_idx = (addr >> 39) & 0x1FF;
		int pdpt_idx = (addr >> 30) & 0x1FF;
		int pd_idx   = (addr >> 21) & 0x1FF;
		int pt_idx   = (addr >> 12) & 0x1FF;

		struct sl_hv_ept_entry *pdpt, *pd, *pt;
		struct page *page;
		phys_addr_t hpa;

		pdpt = ept_get_or_create_table(vm->ept_pml4, pml4_idx);
		pd   = ept_get_or_create_table(pdpt, pdpt_idx);
		pt   = ept_get_or_create_table(pd, pd_idx);
		if (!pt)
			return -ENOMEM;

		/* Pin user page and get physical address */
		if (get_user_pages_fast(hva + offset, 1, FOLL_WRITE, &page) != 1)
			return -EFAULT;

		hpa = page_to_phys(page);
		pt[pt_idx].val = hpa | EPT_READ | EPT_WRITE | EPT_MEM_TYPE_WB;
		if (flags & 0x4) /* exec */
			pt[pt_idx].val |= EPT_EXEC;
	}
	return 0;
}

void sl_hv_ept_destroy(struct sl_hv_vm *vm)
{
	/* Walk all 4 levels, free_page() on each allocated table.
	 * Production code walks recursively; abbreviated here. */
	if (vm->ept_pml4) {
		/* ... (recursive free of PDPT/PD/PT pages, unpin user pages) */
		free_page((unsigned long)vm->ept_pml4);
		vm->ept_pml4 = NULL;
	}
}
```

### Task 2.5: hv_intercept.c — VM-Exit Handler Dispatch

- [ ] Create `kernel/hypervisor/hv_intercept.c`

```c
// SPDX-License-Identifier: GPL-2.0-only
#include <linux/ktime.h>
#include "hv.h"

static int handle_cpuid(struct sl_hv_vcpu *vcpu)
{
	/*
	 * Read guest EAX (leaf), execute CPUID on host,
	 * write results back to guest registers.
	 * Filter sensitive leaves (e.g., mask hypervisor bit).
	 */
	__u64 rip = sl_hv_vmcs_read(vcpu, VMCS_GUEST_RIP);
	sl_hv_vmcs_write(vcpu, VMCS_GUEST_RIP, rip + 2); /* skip CPUID insn */
	return 0;
}

static int handle_io(struct sl_hv_vm *vm, struct sl_hv_vcpu *vcpu,
		      struct sl_hv_vcpu_run *run)
{
	/*
	 * Decode I/O port and direction from exit qualification.
	 * Forward to userspace for emulation via run->exit_reason.
	 */
	run->exit_reason = SL_HV_EXIT_IO;
	run->exit_qualification = sl_hv_vmcs_read(vcpu,
						   VMCS_EXIT_QUALIFICATION);
	return 1; /* return to userspace */
}

static int handle_ept_violation(struct sl_hv_vm *vm, struct sl_hv_vcpu *vcpu,
				 struct sl_hv_vcpu_run *run)
{
	run->exit_reason = SL_HV_EXIT_EPT_VIOLATION;
	run->exit_qualification = sl_hv_vmcs_read(vcpu,
						   VMCS_EXIT_QUALIFICATION);
	return 1; /* userspace must map the page */
}

int sl_hv_handle_exit(struct sl_hv_vm *vm, struct sl_hv_vcpu *vcpu,
		       struct sl_hv_vcpu_run *run)
{
	__u32 reason = sl_hv_vmcs_read(vcpu, VMCS_VM_EXIT_REASON) & 0xFFFF;
	ktime_t start = ktime_get();
	int ret;

	switch (reason) {
	case SL_HV_EXIT_CPUID:
		ret = handle_cpuid(vcpu);
		break;
	case SL_HV_EXIT_HLT:
		run->exit_reason = SL_HV_EXIT_HLT;
		ret = 1;
		break;
	case SL_HV_EXIT_IO:
		ret = handle_io(vm, vcpu, run);
		break;
	case SL_HV_EXIT_EPT_VIOLATION:
		ret = handle_ept_violation(vm, vcpu, run);
		break;
	case SL_HV_EXIT_VMCALL:
		/* Hypercall interface — advance RIP, decode in userspace */
		run->exit_reason = SL_HV_EXIT_VMCALL;
		ret = 1;
		break;
	default:
		pr_warn("straylight-hv: unhandled exit reason %u\n", reason);
		run->exit_reason = reason;
		ret = 1;
		break;
	}

	sl_hv_profiler_record_exit(vm, reason,
				    ktime_to_ns(ktime_sub(ktime_get(), start)));
	return ret; /* 0 = re-enter guest, 1 = return to userspace */
}
```

### Task 2.6: hv_profiler.c — VM-Exit Statistics

- [ ] Create `kernel/hypervisor/hv_profiler.c`

```c
// SPDX-License-Identifier: GPL-2.0-only
#include "hv.h"

void sl_hv_profiler_record_exit(struct sl_hv_vm *vm, __u32 reason, __u64 ns)
{
	if (reason < SL_HV_EXIT_MAX)
		vm->stats.exit_counts[reason]++;
	vm->stats.total_exits++;
	vm->stats.total_run_ns += ns;
}
```

### Task 2.7: Kbuild and DKMS Config

- [ ] Create `kernel/hypervisor/Kbuild`

```makefile
obj-m := straylight-hypervisor.o
straylight-hypervisor-y := hv_main.o hv_vmcs.o hv_memory.o hv_intercept.o hv_profiler.o
ccflags-y := -DDEBUG -std=gnu11
```

- [ ] Create `kernel/dkms/straylight-hypervisor-dkms/dkms.conf`

```ini
PACKAGE_NAME="straylight-hypervisor"
PACKAGE_VERSION="1.0.0"
BUILT_MODULE_NAME[0]="straylight-hypervisor"
BUILT_MODULE_LOCATION[0]="kernel/hypervisor/"
DEST_MODULE_LOCATION[0]="/extra"
MAKE[0]="make -C ${kernel_source_dir} M=${dkms_tree}/${PACKAGE_NAME}/${PACKAGE_VERSION}/build/kernel/hypervisor modules"
CLEAN="make -C ${kernel_source_dir} M=${dkms_tree}/${PACKAGE_NAME}/${PACKAGE_VERSION}/build/kernel/hypervisor clean"
AUTOINSTALL="yes"
```

---

## Chunk 3: straylight-scheduler.ko — ML-Aware Scheduler Extensions

### File Structure

```
kernel/scheduler/
├── Kbuild
├── sched.h
├── sched_main.c
├── sched_ml.c
├── sched_topology.c
kernel/dkms/straylight-scheduler-dkms/
└── dkms.conf
```

### Task 3.1: sched.h — Task Hints and Topology Structures

- [ ] Create `kernel/scheduler/sched.h`

```c
#ifndef _STRAYLIGHT_SCHED_H
#define _STRAYLIGHT_SCHED_H

#include <linux/types.h>

#define SL_SCHED_PROCFS_DIR    "straylight"
#define SL_SCHED_PROCFS_ENTRY  "sched"
#define SL_SCHED_MAX_TASKS     8192

/* Task classification (written by userspace daemon via procfs) */
enum sl_task_class {
	SL_TASK_CLASS_UNKNOWN   = 0,
	SL_TASK_CLASS_ML_TRAIN  = 1,  /* GPU-bound, needs P-cores + memory BW */
	SL_TASK_CLASS_ML_INFER  = 2,  /* Latency-sensitive, P-core preferred */
	SL_TASK_CLASS_IO_BOUND  = 3,  /* E-core is fine */
	SL_TASK_CLASS_REALTIME  = 4,  /* Dedicated P-core, no migration */
	SL_TASK_CLASS_BATCH     = 5,  /* Low priority, E-cores preferred */
	SL_TASK_CLASS_MAX,
};

/* Placement hint written to /proc/straylight/sched */
struct sl_sched_hint {
	__s32 pid;
	__u32 task_class;         /* enum sl_task_class */
	__s32 preferred_cpu;      /* -1 = auto */
	__s32 preferred_numa;     /* -1 = auto */
	__u32 priority_boost;     /* 0-10, added to nice priority */
	__u32 flags;
#define SL_SCHED_FLAG_PIN_CPU    BIT(0)  /* hard-pin to preferred_cpu */
#define SL_SCHED_FLAG_NO_MIGRATE BIT(1)  /* disable cross-NUMA migration */
#define SL_SCHED_FLAG_BOOST_IO   BIT(2)  /* boost on I/O completion */
};

/* Topology snapshot exposed to userspace */
struct sl_topology_info {
	__u32 num_cpus;
	__u32 num_p_cores;
	__u32 num_e_cores;
	__u32 num_numa_nodes;
	__u32 cpu_to_core_type[NR_CPUS]; /* 0=unknown, 1=P-core, 2=E-core */
	__u32 cpu_to_numa[NR_CPUS];
};

#ifdef __KERNEL__

#include <linux/hashtable.h>
#include <linux/proc_fs.h>
#include <linux/spinlock.h>

struct sl_sched_task_entry {
	struct hlist_node node;
	struct sl_sched_hint hint;
};

struct sl_sched_device {
	struct proc_dir_entry *proc_dir;
	struct proc_dir_entry *proc_sched;
	struct proc_dir_entry *proc_topology;
	DECLARE_HASHTABLE(task_hints, 13); /* 8192 buckets */
	spinlock_t lock;
	struct sl_topology_info topology;
};

extern struct sl_sched_device *sl_sched_dev;

/* sched_ml.c */
int sl_sched_ml_classify(struct sl_sched_device *dev, pid_t pid);
int sl_sched_ml_apply_hint(struct sl_sched_device *dev,
			    const struct sl_sched_hint *hint);

/* sched_topology.c */
int sl_sched_topology_init(struct sl_sched_device *dev);
int sl_sched_pick_cpu(struct sl_sched_device *dev,
		       enum sl_task_class class, __s32 preferred_numa);

#endif /* __KERNEL__ */
#endif /* _STRAYLIGHT_SCHED_H */
```

### Task 3.2: sched_main.c — Module Init with procfs Interface

- [ ] Create `kernel/scheduler/sched_main.c`

```c
// SPDX-License-Identifier: GPL-2.0-only
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include "sched.h"

struct sl_sched_device *sl_sched_dev;

/*
 * /proc/straylight/sched — write a sl_sched_hint struct to set placement.
 * Read returns all active hints as newline-delimited text.
 */
static ssize_t sl_sched_proc_write(struct file *file, const char __user *buf,
				    size_t count, loff_t *ppos)
{
	struct sl_sched_hint hint;
	struct sl_sched_task_entry *entry;

	if (count != sizeof(hint))
		return -EINVAL;
	if (copy_from_user(&hint, buf, sizeof(hint)))
		return -EFAULT;
	if (hint.task_class >= SL_TASK_CLASS_MAX)
		return -EINVAL;

	/* Apply to kernel scheduler */
	if (sl_sched_ml_apply_hint(sl_sched_dev, &hint))
		return -EINVAL;

	/* Store in hash table */
	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	memcpy(&entry->hint, &hint, sizeof(hint));

	spin_lock(&sl_sched_dev->lock);
	hash_add(sl_sched_dev->task_hints, &entry->node, hint.pid);
	spin_unlock(&sl_sched_dev->lock);

	return count;
}

static ssize_t sl_sched_proc_read(struct file *file, char __user *buf,
				   size_t count, loff_t *ppos)
{
	/*
	 * Iterate hash table, format as text lines:
	 * "pid=<N> class=<C> cpu=<C> numa=<N> boost=<B>"
	 * Uses seq_file in production; simplified here.
	 */
	/* ... (standard seq_file iteration over task_hints) */
	return 0;
}

static const struct proc_ops sl_sched_proc_ops = {
	.proc_read  = sl_sched_proc_read,
	.proc_write = sl_sched_proc_write,
};

/* /proc/straylight/topology — read-only topology snapshot */
static ssize_t sl_topo_proc_read(struct file *file, char __user *buf,
				  size_t count, loff_t *ppos)
{
	struct sl_topology_info *t = &sl_sched_dev->topology;
	char tmp[256];
	int len;

	len = snprintf(tmp, sizeof(tmp),
		       "cpus=%u p_cores=%u e_cores=%u numa_nodes=%u\n",
		       t->num_cpus, t->num_p_cores,
		       t->num_e_cores, t->num_numa_nodes);

	return simple_read_from_buffer(buf, count, ppos, tmp, len);
}

static const struct proc_ops sl_topo_proc_ops = {
	.proc_read = sl_topo_proc_read,
};

static int __init sl_sched_init(void)
{
	int ret;

	sl_sched_dev = kzalloc(sizeof(*sl_sched_dev), GFP_KERNEL);
	if (!sl_sched_dev)
		return -ENOMEM;

	spin_lock_init(&sl_sched_dev->lock);
	hash_init(sl_sched_dev->task_hints);

	ret = sl_sched_topology_init(sl_sched_dev);
	if (ret)
		goto err_free;

	sl_sched_dev->proc_dir = proc_mkdir(SL_SCHED_PROCFS_DIR, NULL);
	if (!sl_sched_dev->proc_dir) {
		ret = -ENOMEM;
		goto err_free;
	}

	sl_sched_dev->proc_sched = proc_create(SL_SCHED_PROCFS_ENTRY, 0660,
						sl_sched_dev->proc_dir,
						&sl_sched_proc_ops);
	if (!sl_sched_dev->proc_sched) {
		ret = -ENOMEM;
		goto err_proc_dir;
	}

	sl_sched_dev->proc_topology = proc_create("topology", 0444,
						   sl_sched_dev->proc_dir,
						   &sl_topo_proc_ops);
	if (!sl_sched_dev->proc_topology) {
		ret = -ENOMEM;
		goto err_proc_sched;
	}

	pr_info("straylight-sched: initialized (%u CPUs, %u P-cores, %u E-cores)\n",
		sl_sched_dev->topology.num_cpus,
		sl_sched_dev->topology.num_p_cores,
		sl_sched_dev->topology.num_e_cores);
	return 0;

err_proc_sched:
	proc_remove(sl_sched_dev->proc_sched);
err_proc_dir:
	proc_remove(sl_sched_dev->proc_dir);
err_free:
	kfree(sl_sched_dev);
	return ret;
}

static void __exit sl_sched_exit(void)
{
	struct sl_sched_task_entry *entry;
	struct hlist_node *tmp;
	int bkt;

	/* Free all hint entries */
	spin_lock(&sl_sched_dev->lock);
	hash_for_each_safe(sl_sched_dev->task_hints, bkt, tmp, entry, node) {
		hash_del(&entry->node);
		kfree(entry);
	}
	spin_unlock(&sl_sched_dev->lock);

	proc_remove(sl_sched_dev->proc_topology);
	proc_remove(sl_sched_dev->proc_sched);
	proc_remove(sl_sched_dev->proc_dir);
	kfree(sl_sched_dev);
	pr_info("straylight-sched: unloaded\n");
}

module_init(sl_sched_init);
module_exit(sl_sched_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("StrayLight Project");
MODULE_DESCRIPTION("StrayLight ML-aware scheduler extensions");
```

### Task 3.3: sched_ml.c — ML Task Classification and Priority Hints

- [ ] Create `kernel/scheduler/sched_ml.c`

```c
// SPDX-License-Identifier: GPL-2.0-only
#include <linux/sched.h>
#include <linux/pid.h>
#include <linux/cpumask.h>
#include "sched.h"

/*
 * Classify a task by PID. Reads /proc/<pid>/status characteristics
 * to guess task class. Called when userspace hasn't provided explicit hints.
 */
int sl_sched_ml_classify(struct sl_sched_device *dev, pid_t pid)
{
	struct task_struct *task;
	int class = SL_TASK_CLASS_UNKNOWN;

	rcu_read_lock();
	task = find_task_by_vpid(pid);
	if (!task) {
		rcu_read_unlock();
		return -ESRCH;
	}
	get_task_struct(task);
	rcu_read_unlock();

	/*
	 * Heuristic classification:
	 * - High CPU usage + GPU device open → ML_TRAIN
	 * - Moderate CPU + low latency needs → ML_INFER
	 * - High iowait ratio → IO_BOUND
	 * - SCHED_FIFO/RR → REALTIME
	 * - Nice >= 10 → BATCH
	 */
	if (task->policy == SCHED_FIFO || task->policy == SCHED_RR)
		class = SL_TASK_CLASS_REALTIME;
	else if (task_nice(task) >= 10)
		class = SL_TASK_CLASS_BATCH;
	/* ... (more heuristics based on /proc/<pid>/io, cgroup membership) */

	put_task_struct(task);
	return class;
}

/*
 * Apply a scheduling hint: set CPU affinity, adjust nice, pin to NUMA.
 * This is the kernel-side enforcement of userspace policy decisions.
 */
int sl_sched_ml_apply_hint(struct sl_sched_device *dev,
			    const struct sl_sched_hint *hint)
{
	struct task_struct *task;
	cpumask_var_t mask;
	int target_cpu;

	rcu_read_lock();
	task = find_task_by_vpid(hint->pid);
	if (!task) {
		rcu_read_unlock();
		return -ESRCH;
	}
	get_task_struct(task);
	rcu_read_unlock();

	/* Pick optimal CPU based on class and topology */
	if (hint->preferred_cpu >= 0 && (hint->flags & SL_SCHED_FLAG_PIN_CPU))
		target_cpu = hint->preferred_cpu;
	else
		target_cpu = sl_sched_pick_cpu(dev, hint->task_class,
					       hint->preferred_numa);

	if (target_cpu >= 0 && alloc_cpumask_var(&mask, GFP_KERNEL)) {
		cpumask_clear(mask);

		if (hint->flags & SL_SCHED_FLAG_PIN_CPU) {
			cpumask_set_cpu(target_cpu, mask);
		} else if (hint->flags & SL_SCHED_FLAG_NO_MIGRATE) {
			/* Allow all CPUs on the same NUMA node */
			int cpu;
			for_each_online_cpu(cpu) {
				if (dev->topology.cpu_to_numa[cpu] ==
				    dev->topology.cpu_to_numa[target_cpu])
					cpumask_set_cpu(cpu, mask);
			}
		} else {
			cpumask_copy(mask, cpu_online_mask);
		}

		sched_setaffinity(task->pid, mask);
		free_cpumask_var(mask);
	}

	/* Apply priority boost via set_user_nice */
	if (hint->priority_boost > 0) {
		int new_nice = task_nice(task) - (int)hint->priority_boost;
		if (new_nice < -20)
			new_nice = -20;
		set_user_nice(task, new_nice);
	}

	put_task_struct(task);
	return 0;
}
```

### Task 3.4: sched_topology.c — NUMA + P/E-Core Discovery

- [ ] Create `kernel/scheduler/sched_topology.c`

```c
// SPDX-License-Identifier: GPL-2.0-only
#include <linux/cpu.h>
#include <linux/topology.h>
#include <linux/cpufreq.h>
#include <asm/cpu.h>
#include "sched.h"

/*
 * Detect Intel hybrid (Alder Lake+) P-core vs E-core.
 * Uses CPUID leaf 0x1A (Native Model ID) on each CPU.
 */
#define CPUID_HYBRID_LEAF   0x1A
#define CORE_TYPE_PCORE     0x40   /* Intel Core type */
#define CORE_TYPE_ECORE     0x20   /* Intel Atom type */

static void detect_core_type(void *info)
{
	struct sl_topology_info *topo = info;
	unsigned int eax, ebx, ecx, edx;
	int cpu = smp_processor_id();

	if (cpuid_eax(0) < CPUID_HYBRID_LEAF) {
		topo->cpu_to_core_type[cpu] = 1; /* assume P-core if no hybrid */
		return;
	}

	cpuid(CPUID_HYBRID_LEAF, &eax, &ebx, &ecx, &edx);
	switch ((eax >> 24) & 0xFF) {
	case CORE_TYPE_PCORE:
		topo->cpu_to_core_type[cpu] = 1;
		break;
	case CORE_TYPE_ECORE:
		topo->cpu_to_core_type[cpu] = 2;
		break;
	default:
		topo->cpu_to_core_type[cpu] = 1; /* default to P-core */
	}
}

int sl_sched_topology_init(struct sl_sched_device *dev)
{
	int cpu;
	struct sl_topology_info *t = &dev->topology;

	t->num_cpus = num_online_cpus();
	t->num_numa_nodes = num_online_nodes();
	t->num_p_cores = 0;
	t->num_e_cores = 0;

	/* Detect NUMA topology */
	for_each_online_cpu(cpu)
		t->cpu_to_numa[cpu] = cpu_to_node(cpu);

	/* Detect P/E core types — must run on each CPU */
	on_each_cpu(detect_core_type, t, 1);

	for_each_online_cpu(cpu) {
		if (t->cpu_to_core_type[cpu] == 1)
			t->num_p_cores++;
		else if (t->cpu_to_core_type[cpu] == 2)
			t->num_e_cores++;
	}

	return 0;
}

/*
 * Pick best CPU for a given task class and NUMA preference.
 * Returns CPU number or -1 if no preference.
 */
int sl_sched_pick_cpu(struct sl_sched_device *dev,
		       enum sl_task_class class, __s32 preferred_numa)
{
	struct sl_topology_info *t = &dev->topology;
	int cpu, best = -1;
	int target_type;

	switch (class) {
	case SL_TASK_CLASS_ML_TRAIN:
	case SL_TASK_CLASS_ML_INFER:
	case SL_TASK_CLASS_REALTIME:
		target_type = 1; /* P-core */
		break;
	case SL_TASK_CLASS_IO_BOUND:
	case SL_TASK_CLASS_BATCH:
		target_type = 2; /* E-core preferred */
		break;
	default:
		return -1; /* no preference */
	}

	for_each_online_cpu(cpu) {
		if (t->cpu_to_core_type[cpu] != target_type)
			continue;
		if (preferred_numa >= 0 &&
		    t->cpu_to_numa[cpu] != (__u32)preferred_numa)
			continue;
		best = cpu;
		break; /* first match — production code picks least-loaded */
	}

	/* Fallback: if no E-cores found, any CPU is fine */
	if (best < 0 && target_type == 2) {
		for_each_online_cpu(cpu) {
			if (preferred_numa >= 0 &&
			    t->cpu_to_numa[cpu] != (__u32)preferred_numa)
				continue;
			best = cpu;
			break;
		}
	}

	return best;
}
```

### Task 3.5: Kbuild and DKMS Config

- [ ] Create `kernel/scheduler/Kbuild`

```makefile
obj-m := straylight-scheduler.o
straylight-scheduler-y := sched_main.o sched_ml.o sched_topology.o
ccflags-y := -DDEBUG -std=gnu11
```

- [ ] Create `kernel/dkms/straylight-scheduler-dkms/dkms.conf`

```ini
PACKAGE_NAME="straylight-scheduler"
PACKAGE_VERSION="1.0.0"
BUILT_MODULE_NAME[0]="straylight-scheduler"
BUILT_MODULE_LOCATION[0]="kernel/scheduler/"
DEST_MODULE_LOCATION[0]="/extra"
MAKE[0]="make -C ${kernel_source_dir} M=${dkms_tree}/${PACKAGE_NAME}/${PACKAGE_VERSION}/build/kernel/scheduler modules"
CLEAN="make -C ${kernel_source_dir} M=${dkms_tree}/${PACKAGE_NAME}/${PACKAGE_VERSION}/build/kernel/scheduler clean"
AUTOINSTALL="yes"
```

---

## Chunk 4: Integration, Testing, and Verification

### Task 4.1: Module Load/Unload Smoke Tests

- [ ] Create `tests/kernel/test_vpu_load.sh`

```bash
#!/bin/bash
set -euo pipefail

echo "=== straylight-vpu smoke test ==="
sudo insmod kernel/vpu/straylight-vpu.ko
test -c /dev/straylight-vpu || { echo "FAIL: device not created"; exit 1; }
test -f /sys/kernel/straylight-vpu/memory_used || { echo "FAIL: sysfs missing"; exit 1; }

# Verify sysfs reads
MEM=$(cat /sys/kernel/straylight-vpu/memory_used)
echo "memory_used=$MEM (expected 0)"
[ "$MEM" = "0" ] || { echo "FAIL: expected 0"; exit 1; }

sudo rmmod straylight-vpu
echo "PASS"
```

- [ ] Create `tests/kernel/test_hv_load.sh`

```bash
#!/bin/bash
set -euo pipefail

echo "=== straylight-hypervisor smoke test ==="
# Requires VT-x — skip in CI without nested virt
if ! grep -q vmx /proc/cpuinfo 2>/dev/null; then
    echo "SKIP: VT-x not available"
    exit 0
fi

sudo insmod kernel/hypervisor/straylight-hypervisor.ko
test -c /dev/straylight-hv || { echo "FAIL: device not created"; exit 1; }
sudo rmmod straylight-hypervisor
echo "PASS"
```

- [ ] Create `tests/kernel/test_sched_load.sh`

```bash
#!/bin/bash
set -euo pipefail

echo "=== straylight-scheduler smoke test ==="
sudo insmod kernel/scheduler/straylight-scheduler.ko
test -f /proc/straylight/sched || { echo "FAIL: procfs missing"; exit 1; }
test -f /proc/straylight/topology || { echo "FAIL: topology missing"; exit 1; }

# Read topology
TOPO=$(cat /proc/straylight/topology)
echo "topology: $TOPO"
echo "$TOPO" | grep -q "cpus=" || { echo "FAIL: bad topology format"; exit 1; }

sudo rmmod straylight-scheduler
echo "PASS"
```

### Task 4.2: Build All Three Modules

- [ ] Verify Kbuild for all three modules:

```bash
# Build all three (requires linux-headers)
make -C /lib/modules/$(uname -r)/build M=$(pwd)/kernel/vpu modules
make -C /lib/modules/$(uname -r)/build M=$(pwd)/kernel/hypervisor modules
make -C /lib/modules/$(uname -r)/build M=$(pwd)/kernel/scheduler modules
```

### Task 4.3: DKMS Registration

- [ ] Register all three modules with DKMS:

```bash
# Copy sources to DKMS tree
for mod in vpu hypervisor scheduler; do
    sudo cp -r kernel/$mod /usr/src/straylight-$mod-1.0.0/
    sudo dkms add -m straylight-$mod -v 1.0.0
    sudo dkms build -m straylight-$mod -v 1.0.0
    sudo dkms install -m straylight-$mod -v 1.0.0
done
```

### Task 4.4: Verification Checklist

- [ ] `straylight-vpu.ko` loads, creates `/dev/straylight-vpu`, sysfs attributes readable
- [ ] VPU ioctl ALLOC returns valid handle, FREE succeeds, QUERY reports correct stats
- [ ] VPU DMA-BUF export succeeds (requires DMA-capable device or stub)
- [ ] `straylight-hypervisor.ko` loads on VT-x capable CPU, creates `/dev/straylight-hv`
- [ ] Hypervisor CREATE_VM returns vm_id, MAP_MEMORY succeeds, GET_STATS returns zeroed counters
- [ ] `straylight-scheduler.ko` loads, creates `/proc/straylight/sched` and `/proc/straylight/topology`
- [ ] Topology detection reports correct CPU count, P/E-core classification, NUMA nodes
- [ ] Writing a `sl_sched_hint` to `/proc/straylight/sched` changes task affinity (verify with `taskset -p`)
- [ ] All three modules unload cleanly with no kernel warnings in `dmesg`
- [ ] DKMS build succeeds for current kernel version
- [ ] No memory leaks reported by `kmemleak` after load/unload cycle
