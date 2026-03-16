// SPDX-License-Identifier: GPL-2.0
/*
 * StrayLight OS — Enclave Page Cache Management
 * Copyright (C) 2026 StrayLight Systems
 *
 * Manages the SGX Enclave Page Cache (EPC) via ENCLS privileged
 * instructions.  Implements the three-stage enclave build sequence:
 *
 *   1. ECREATE — allocate SECS page, define enclave virtual layout
 *   2. EADD    — copy one page from normal memory into EPC + EEXTEND
 *   3. EINIT   — finalise measurement, enable execution
 *
 * After EINIT the enclave can be entered via EENTER (ring 3).
 *
 * SGX ENCLS instruction:
 *   Leaf in EAX, RBX = operand 1, RCX = operand 2, RDX = operand 3.
 *   Return value in EAX: 0 = success, non-zero = SGX_ERROR code.
 *
 * Reference: Intel SDM Vol. 3D, Chapters 38-40.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/atomic.h>
#include <asm/processor.h>
#include <asm/cpufeature.h>

#include "enclave.h"

/* ---- EPC descriptor table ---------------------------------------------- */

static LIST_HEAD(enclave_list);
static DEFINE_MUTEX(enclave_mutex);
static u64 next_enclave_id = 1;

/* ---- CPUID-based SGX feature detection --------------------------------- */

/*
 * SGX is reported in CPUID leaf 7, sub-leaf 0, EBX bit 2.
 * SGX1 capability is in CPUID leaf 0x12, sub-leaf 0, EAX bit 0.
 */
bool sl_epc_detect_sgx(void)
{
#ifdef CONFIG_X86_64
	u32 eax, ebx, ecx, edx;

	/* CPUID.07H:EBX.SGX[bit 2] */
	cpuid_count(7, 0, &eax, &ebx, &ecx, &edx);
	if (!(ebx & (1U << 2))) {
		pr_info("straylight-enclave: CPUID.07H SGX bit not set\n");
		return false;
	}

	/* CPUID.12H:EAX.SGX1[bit 0] */
	cpuid_count(0x12, 0, &eax, &ebx, &ecx, &edx);
	if (!(eax & (1U << 0))) {
		pr_info("straylight-enclave: SGX1 capability not available\n");
		return false;
	}

	pr_info("straylight-enclave: SGX1 detected\n");
	return true;
#else
	return false;
#endif
}

/* ---- ENCLS instruction wrapper ---------------------------------------- */

/*
 * __encls — execute one ENCLS leaf instruction.
 *
 * @leaf:  EAX  — instruction selector (SGX_ECREATE, SGX_EADD, ...)
 * @rbx:   RBX  — first operand (often a pointer)
 * @rcx:   RCX  — second operand
 * @rdx:   RDX  — third operand
 *
 * Returns the SGX error code from EAX; 0 = success.
 *
 * The ENCLS instruction faults (#UD) if executed on a CPU that does not
 * support SGX.  Callers must gate on sl_epc_detect_sgx() before calling.
 */
static __always_inline int __encls(u32 leaf, unsigned long rbx,
				   unsigned long rcx, unsigned long rdx)
{
	int ret;

	asm volatile(
		/* Serialise before the privileged instruction */
		"lfence\n\t"
		".byte 0x0f, 0x01, 0xcf\n\t" /* ENCLS opcode */
		: "=a" (ret)
		: "a" (leaf), "b" (rbx), "c" (rcx), "d" (rdx)
		: "memory", "cc"
	);
	return ret;
}

/* ---- Helper: look up enclave by ID ------------------------------------- */

static struct sl_enclave *epc_find_locked(u64 id)
{
	struct sl_enclave *enc;

	list_for_each_entry(enc, &enclave_list, list) {
		if (enc->id == id)
			return enc;
	}
	return NULL;
}

/* ---- Module init / exit ----------------------------------------------- */

int sl_epc_init(void)
{
	pr_info("straylight-enclave: EPC manager ready\n");
	return 0;
}

void sl_epc_cleanup(void)
{
	struct sl_enclave *enc, *tmp;

	mutex_lock(&enclave_mutex);
	list_for_each_entry_safe(enc, tmp, &enclave_list, list) {
		/*
		 * EREMOVE each page before freeing the descriptor.
		 * In production this would walk all EPC pages associated
		 * with the enclave; here we free the descriptor only.
		 */
		list_del(&enc->list);
		kfree(enc);
	}
	mutex_unlock(&enclave_mutex);
}

/* ---- SL_SGX_IOC_CREATE — ECREATE -------------------------------------- */

/*
 * sl_epc_create — create a new SGX enclave.
 *
 * Allocates a SECS EPC page, fills it with the caller-supplied
 * parameters, and executes ECREATE.  Returns the new enclave handle
 * via arg->enclave_id.
 */
long sl_epc_create(struct sl_sgx_create __user *uarg)
{
	struct sl_sgx_create params;
	struct sl_secs *secs = NULL;
	struct sl_enclave *enc = NULL;
	int sgx_ret;
	long ret = 0;

	if (copy_from_user(&params, uarg, sizeof(params)))
		return -EFAULT;

	/* Validate: base must be page-aligned, size must be power of two */
	if (!IS_ALIGNED(params.base_addr, PAGE_SIZE)) {
		pr_debug("straylight-enclave: base_addr not page-aligned\n");
		return -EINVAL;
	}
	if (!is_power_of_2(params.size) || params.size < PAGE_SIZE) {
		pr_debug("straylight-enclave: invalid enclave size\n");
		return -EINVAL;
	}

	/* Allocate and zero a SECS structure (must be 4 KB aligned) */
	secs = (struct sl_secs *)get_zeroed_page(GFP_KERNEL);
	if (!secs)
		return -ENOMEM;

	secs->size       = params.size;
	secs->base       = params.base_addr;
	secs->attributes = params.secs_attr;
	/*
	 * SSA frame size of 1 is the minimum for non-debug enclaves.
	 * The value is encoded in pages.
	 */
	secs->ssa_frame_size = 1;

	/* Allocate the kernel descriptor */
	enc = kzalloc(sizeof(*enc), GFP_KERNEL);
	if (!enc) {
		ret = -ENOMEM;
		goto err_secs;
	}

	INIT_LIST_HEAD(&enc->list);
	atomic_set(&enc->refcount, 1);
	enc->base       = params.base_addr;
	enc->size       = params.size;
	enc->attributes = params.secs_attr;
	enc->initialized = false;

	/*
	 * ECREATE:
	 *   EAX = SGX_ECREATE
	 *   RBX = pointer to SECS in normal memory (4 KB aligned)
	 *   RCX = address of EPC page to receive SECS
	 *         (the hardware manages EPC allocation; here we pass the
	 *          base VA — on real hardware this is an EPC-allocated PA)
	 */
	sgx_ret = __encls(SGX_ECREATE,
			  (unsigned long)secs,
			  (unsigned long)params.base_addr,
			  0);
	if (sgx_ret) {
		pr_debug("straylight-enclave: ECREATE failed with SGX error "
			 "0x%x\n", sgx_ret);
		ret = -EIO;
		goto err_enc;
	}

	mutex_lock(&enclave_mutex);
	enc->id = next_enclave_id++;
	list_add_tail(&enc->list, &enclave_list);
	mutex_unlock(&enclave_mutex);

	/* Return the handle to userspace */
	if (put_user(enc->id, &uarg->enclave_id)) {
		ret = -EFAULT;
		goto err_list;
	}

	free_page((unsigned long)secs);
	return 0;

err_list:
	mutex_lock(&enclave_mutex);
	list_del(&enc->list);
	mutex_unlock(&enclave_mutex);
err_enc:
	kfree(enc);
err_secs:
	free_page((unsigned long)secs);
	return ret;
}

/* ---- SL_SGX_IOC_ADD_PAGE — EADD + EEXTEND ----------------------------- */

/*
 * sl_epc_add_page — add one page to an enclave.
 *
 * The source page is read from userspace, an EPC page is allocated,
 * the hardware copies content via EADD, then EEXTEND hashes each
 * 256-byte chunk into the measurement.
 */
long sl_epc_add_page(struct sl_sgx_add_page __user *uarg)
{
	struct sl_sgx_add_page params;
	struct sl_enclave *enc;
	struct sl_secinfo *secinfo = NULL;
	void *src_page = NULL;
	int sgx_ret;
	long ret = 0;
	u64 chunk;

	if (copy_from_user(&params, uarg, sizeof(params)))
		return -EFAULT;

	if (!IS_ALIGNED(params.offset, PAGE_SIZE))
		return -EINVAL;

	mutex_lock(&enclave_mutex);
	enc = epc_find_locked(params.enclave_id);
	mutex_unlock(&enclave_mutex);

	if (!enc)
		return -ENOENT;
	if (enc->initialized)
		return -EPERM; /* cannot add pages after EINIT */

	if (params.offset >= enc->size)
		return -ERANGE;

	/* Copy source page from userspace */
	src_page = (void *)get_zeroed_page(GFP_KERNEL);
	if (!src_page)
		return -ENOMEM;

	if (copy_from_user(src_page, (void __user *)params.src_addr,
			   PAGE_SIZE)) {
		ret = -EFAULT;
		goto out_src;
	}

	/* Allocate SECINFO (must be 64-byte aligned per SDM) */
	secinfo = kzalloc(sizeof(*secinfo), GFP_KERNEL);
	if (!secinfo) {
		ret = -ENOMEM;
		goto out_src;
	}
	secinfo->flags = params.secinfo_flags;
	if (!(secinfo->flags & SECINFO_PT_MASK))
		secinfo->flags |= SECINFO_PT_REG;  /* default to REG page */

	/*
	 * EADD:
	 *   EAX = SGX_EADD
	 *   RBX = pointer to PAGEINFO structure (SECINFO + source page)
	 *   RCX = target EPC page address (enclave_base + offset)
	 *
	 * For brevity, we pass secinfo directly as RBX and source as RDX;
	 * a production driver would build a full PAGEINFO struct.
	 */
	sgx_ret = __encls(SGX_EADD,
			  (unsigned long)secinfo,
			  enc->base + params.offset,
			  (unsigned long)src_page);
	if (sgx_ret) {
		pr_debug("straylight-enclave: EADD failed 0x%x (offset=%llx)\n",
			 sgx_ret, params.offset);
		ret = -EIO;
		goto out_secinfo;
	}

	/*
	 * EEXTEND: measure each 256-byte chunk of the newly-added page.
	 *   EAX = SGX_EEXTEND
	 *   RCX = address of 256-byte region within the EPC page
	 */
	for (chunk = 0; chunk < PAGE_SIZE; chunk += 256) {
		sgx_ret = __encls(SGX_EEXTEND,
				  0,
				  enc->base + params.offset + chunk,
				  0);
		if (sgx_ret) {
			pr_debug("straylight-enclave: EEXTEND failed 0x%x\n",
				 sgx_ret);
			ret = -EIO;
			goto out_secinfo;
		}
	}

out_secinfo:
	kfree(secinfo);
out_src:
	/* Zeroize the temporary copy of the enclave page */
	memzero_explicit(src_page, PAGE_SIZE);
	free_page((unsigned long)src_page);
	return ret;
}

/* ---- SL_SGX_IOC_INIT — EINIT ------------------------------------------ */

/*
 * sl_epc_init_enclave — finalise an enclave.
 *
 * EINIT verifies the SIGSTRUCT signature against the enclave measurement,
 * checks the launch token (EINITTOKEN), and transitions the SECS to the
 * INITIALIZED state.  After this call the enclave can be entered.
 */
long sl_epc_init_enclave(struct sl_sgx_init_param __user *uarg)
{
	struct sl_sgx_init_param *params;
	struct sl_enclave *enc;
	int sgx_ret;
	long ret = 0;

	params = kmalloc(sizeof(*params), GFP_KERNEL);
	if (!params)
		return -ENOMEM;

	if (copy_from_user(params, uarg, sizeof(*params))) {
		ret = -EFAULT;
		goto out;
	}

	mutex_lock(&enclave_mutex);
	enc = epc_find_locked(params->enclave_id);
	mutex_unlock(&enclave_mutex);

	if (!enc) {
		ret = -ENOENT;
		goto out;
	}
	if (enc->initialized) {
		ret = -EALREADY;
		goto out;
	}

	/*
	 * EINIT:
	 *   EAX = SGX_EINIT
	 *   RBX = pointer to SIGSTRUCT (1808 bytes, 4 KB aligned recommended)
	 *   RCX = address of SECS EPC page (enclave base)
	 *   RDX = pointer to EINITTOKEN (304 bytes)
	 */
	sgx_ret = __encls(SGX_EINIT,
			  (unsigned long)params->sigstruct,
			  enc->base,
			  (unsigned long)params->einittoken);
	if (sgx_ret) {
		pr_warn("straylight-enclave: EINIT failed with SGX error "
			"0x%x (enclave_id=%llu)\n", sgx_ret, enc->id);
		ret = -EIO;
		goto out;
	}

	enc->initialized = true;
	pr_info("straylight-enclave: enclave %llu initialized\n", enc->id);

out:
	/* Zeroize the sensitive key material before freeing */
	memzero_explicit(params->sigstruct, sizeof(params->sigstruct));
	memzero_explicit(params->einittoken, sizeof(params->einittoken));
	kfree(params);
	return ret;
}
