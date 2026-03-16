// SPDX-License-Identifier: GPL-2.0
/*
 * StrayLight OS — Enclave Page Cache Management
 * Copyright (C) 2026 StrayLight Systems
 *
 * Manages SGX Enclave Page Cache (EPC) allocations and executes the
 * privileged ENCLS instructions that build and finalise enclaves:
 *
 *   ECREATE  — initialise the SECS, define the enclave virtual layout
 *   EADD     — copy one page of code or data from normal memory → EPC
 *   EEXTEND  — extend the enclave measurement (SHA-256) over 256-byte chunks
 *   EINIT    — verify SIGSTRUCT, lock measurement, allow EENTER
 *
 * EPC page allocation on real hardware is managed by the CPU's Memory
 * Encryption Engine (MEE).  In this driver we model EPC pages as
 * kernel-allocated struct pages that can be physically mapped; on actual
 * SGX hardware the OS would configure the EWB/ELB page management instead.
 *
 * ENCLS opcode: .byte 0x0f, 0x01, 0xcf
 *   EAX = leaf selector
 *   RBX = operand 1 (pointer to input structure, or 0)
 *   RCX = operand 2 (EPC page linear address)
 *   RDX = operand 3 (auxiliary pointer, or 0)
 *   Returns SGX error code in EAX; 0 = success.
 *
 * Reference: Intel SDM Vol. 3D, §38 (ENCLS Instruction Reference).
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/gfp.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/spinlock.h>
#include <asm/processor.h>
#include <asm/cpufeature.h>

#include "enclave.h"

/*
 * We need access to the per-enclave and per-device structures that live
 * in enclave_main.c.  Rather than putting them in a separate header
 * (which risks conflicting with enclave_main's local definitions), we
 * declare minimal extern structs here that mirror what enclave_main
 * exposes via forward declarations.
 *
 * The structs below MUST remain in sync with enclave_main.c.
 */

#define SL_SGX_MAX_PAGES        4096

struct sl_sgx_enclave {
	struct list_head        list;
	__u32                   id;
	__u64                   size;
	__u64                   base_addr;
	bool                    initialized;
	unsigned int            nr_pages;

	struct page             *epc_pages[SL_SGX_MAX_PAGES];
	u64                     epc_phys[SL_SGX_MAX_PAGES];

	struct page             *secs_page;
	u64                     secs_phys;

	u8                      seal_key[16];
	bool                    seal_key_valid;

	struct mutex            lock;
};

struct sl_sgx_device {
	/* We only need the EPC spinlock and counters from here */
	struct miscdevice       miscdev;   /* keep layout identical */
	struct list_head        enclave_list;
	struct mutex            list_lock;
	__u32                   next_id;
	bool                    sgx1_supported;
	bool                    sgx2_supported;
	u64                     epc_base;
	u64                     epc_size;
};

/* ---- ENCLS instruction wrapper ---------------------------------------- */

/*
 * __encls_3 — execute an ENCLS leaf with three register operands.
 *
 * Used for ECREATE, EADD, EINIT which all take RBX, RCX, RDX.
 * Returns the SGX fault code from EAX (0 = success).
 *
 * Note: the .byte sequence for ENCLS is 0x0F 0x01 0xCF.
 * Wrapping it in a macro ensures the assembler always emits it even
 * in translation units compiled without -msgx.
 */
static __always_inline int __encls_3(u32 leaf,
				     unsigned long rbx,
				     unsigned long rcx,
				     unsigned long rdx)
{
	int ret;

	asm volatile(
		"lfence\n\t"
		".byte 0x0f, 0x01, 0xcf\n\t"   /* ENCLS */
		"mfence\n\t"
		: "=a" (ret)
		: "a"  (leaf),
		  "b"  (rbx),
		  "c"  (rcx),
		  "d"  (rdx)
		: "memory", "cc"
	);
	return ret;
}

/*
 * __encls_1 — ENCLS leaf that uses only RCX (e.g. EEXTEND, EREMOVE).
 */
static __always_inline int __encls_1(u32 leaf, unsigned long rcx)
{
	int ret;

	asm volatile(
		"lfence\n\t"
		".byte 0x0f, 0x01, 0xcf\n\t"
		: "=a" (ret)
		: "a"  (leaf),
		  "b"  (0UL),
		  "c"  (rcx),
		  "d"  (0UL)
		: "memory", "cc"
	);
	return ret;
}

/* ---- SGX SECS layout -------------------------------------------------- */

/*
 * Minimal SECS fields we populate before ECREATE.
 * Hardware fills the remainder.  Must be 4 KiB naturally aligned.
 */
struct sl_secs {
	__u64   size;           /* enclave virtual size (power of 2)    */
	__u64   base;           /* enclave base VA                       */
	__u32   ssa_frame_size; /* SSA frame size in pages (minimum: 1)  */
	__u32   miscselect;     /* extended features the CPU will save   */
	__u8    reserved1[24];
	__u64   attributes;     /* XFRM and other attribute flags        */
	__u8    reserved2[136];
	__u16   isvprodid;
	__u16   isvsvn;
	__u8    reserved3[3836];
} __packed;

static_assert(sizeof(struct sl_secs) == PAGE_SIZE, "SECS must be 4 KiB");

/* ---- SECINFO layout --------------------------------------------------- */

struct sl_secinfo {
	__u64   flags;
	__u8    reserved[56];
} __packed;

static_assert(sizeof(struct sl_secinfo) == 64, "SECINFO must be 64 bytes");

/* ---- PAGEINFO layout -------------------------------------------------- */

/*
 * PAGEINFO is the RBX operand for EADD.
 * It bundles source, SECINFO, and EPC destination in one 32-byte struct.
 */
struct sl_pageinfo {
	__u64   linaddr;        /* source page linear address (kernel VA) */
	__u64   secinfo;        /* physical address of SECINFO             */
	__u64   secs;           /* physical address of SECS EPC page       */
	__u64   offset;         /* offset within enclave                   */
} __packed;

/* ---- EPC page allocator ----------------------------------------------- */

/*
 * On real SGX hardware the EPC is a protected DRAM region managed by
 * the MEE.  Pages are allocated via EAUG (SGX2) or pre-assigned at
 * build time (SGX1).  For this driver we emulate EPC allocation by
 * grabbing GFP_KERNEL pages and recording their physical addresses.
 */

int sl_epc_init(struct sl_sgx_device *dev)
{
	pr_info("straylight-enclave: EPC manager initialised "
		"(emulated, %llu MiB EPC from CPUID)\n",
		dev->epc_size >> 20);
	return 0;
}

void sl_epc_cleanup(struct sl_sgx_device *dev)
{
	/* Nothing to tear down in the emulated allocator */
}

int sl_epc_alloc_page(struct sl_sgx_device *dev,
		      struct page **page_out, u64 *phys_out)
{
	struct page *page;

	page = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (!page)
		return -ENOMEM;

	*page_out = page;
	*phys_out = page_to_phys(page);
	return 0;
}

void sl_epc_free_page(struct sl_sgx_device *dev, struct page *page)
{
	if (page) {
		/* Scrub the page before returning to the allocator */
		void *va = kmap_local_page(page);

		memzero_explicit(va, PAGE_SIZE);
		kunmap_local(va);
		__free_page(page);
	}
}

/* ---- ECREATE ---------------------------------------------------------- */

/*
 * sl_epc_ecreate — run ECREATE to initialise the enclave SECS page.
 *
 * The caller has already allocated enc->secs_page via sl_epc_alloc_page.
 * We fill a SECS structure in kernel memory and hand it to ECREATE:
 *
 *   ECREATE(EAX=0, RBX=&secs_template, RCX=epc_secs_pa)
 *
 * On success, the EPC page at enc->secs_phys is the live SECS.
 */
int sl_epc_ecreate(struct sl_sgx_enclave *enc)
{
	struct sl_secs *secs;
	int ret;

	secs = kzalloc(sizeof(*secs), GFP_KERNEL);
	if (!secs)
		return -ENOMEM;

	secs->size           = enc->size;
	secs->base           = enc->base_addr;
	secs->ssa_frame_size = 1;
	secs->miscselect     = 0;
	/*
	 * ATTRIBUTES.DEBUG=0 (production) | ATTRIBUTES.MODE64BIT=1
	 * 0x0000000000000006  — bit 1=DEBUG, bit 2=MODE64BIT, bit 5=KSS
	 */
	secs->attributes     = 0x0000000000000006ULL;

	/*
	 * ENCLS[ECREATE]:
	 *   RBX = linear address of the SECS template (kernel VA)
	 *   RCX = physical address of the EPC destination page
	 */
	ret = __encls_3(SGX_ECREATE,
			(unsigned long)secs,
			enc->secs_phys,
			0);

	memzero_explicit(secs, sizeof(*secs));
	kfree(secs);

	if (ret) {
		pr_debug("straylight-enclave: ECREATE failed SGX error=0x%x "
			 "(enc base=0x%llx size=0x%llx)\n",
			 ret, enc->base_addr, enc->size);
		return -EIO;
	}

	pr_debug("straylight-enclave: ECREATE OK (id=%u base=0x%llx)\n",
		 enc->id, enc->base_addr);
	return 0;
}

/* ---- EADD + EEXTEND --------------------------------------------------- */

/*
 * sl_epc_eadd — add one page to the enclave and extend the measurement.
 *
 * @enc:       the enclave context (already past ECREATE)
 * @offset:    byte offset within the enclave VA (must be page-aligned)
 * @src:       kernel VA of the source page content
 * @page_type: SGX_PT_TCS or SGX_PT_REG
 * @flags:     SECINFO RWX bits
 *
 * EADD copies the source page into the EPC and binds it to the enclave
 * measurement via the SECS.  Each 256-byte chunk must then be extended
 * with EEXTEND so the CPU includes it in the final MRENCLAVE hash.
 */
int sl_epc_eadd(struct sl_sgx_enclave *enc, u64 offset,
		void *src, u32 page_type, u64 flags)
{
	struct sl_secinfo *secinfo;
	struct sl_pageinfo *pageinfo;
	struct page *epc_page;
	u64 epc_phys;
	u64 chunk;
	int ret;
	int page_idx;

	if (!IS_ALIGNED(offset, PAGE_SIZE))
		return -EINVAL;
	if (offset >= enc->size)
		return -ERANGE;
	if (enc->nr_pages >= SL_SGX_MAX_PAGES)
		return -ENOSPC;

	/* Allocate an EPC page for this content */
	ret = sl_epc_alloc_page(NULL, &epc_page, &epc_phys);
	if (ret)
		return ret;

	/* Copy source content into the EPC page before EADD */
	{
		void *dst = kmap_local_page(epc_page);

		memcpy(dst, src, PAGE_SIZE);
		kunmap_local(dst);
	}

	/* Build SECINFO */
	secinfo = kzalloc(sizeof(*secinfo), GFP_KERNEL);
	if (!secinfo) {
		sl_epc_free_page(NULL, epc_page);
		return -ENOMEM;
	}

	secinfo->flags = flags & (SGX_SECINFO_R | SGX_SECINFO_W |
				  SGX_SECINFO_X);
	switch (page_type) {
	case SGX_PT_TCS:
		secinfo->flags |= SGX_SECINFO_PT_TCS;
		break;
	case SGX_PT_TRIM:
		secinfo->flags |= SGX_SECINFO_PT_TRIM;
		break;
	default:
		secinfo->flags |= SGX_SECINFO_PT_REG;
		break;
	}

	/* Build PAGEINFO (RBX for EADD) */
	pageinfo = kzalloc(sizeof(*pageinfo), GFP_KERNEL);
	if (!pageinfo) {
		kfree(secinfo);
		sl_epc_free_page(NULL, epc_page);
		return -ENOMEM;
	}

	pageinfo->linaddr = (u64)(unsigned long)src;
	pageinfo->secinfo = virt_to_phys(secinfo);
	pageinfo->secs    = enc->secs_phys;
	pageinfo->offset  = enc->base_addr + offset;

	/*
	 * ENCLS[EADD]:
	 *   RBX = linear address of PAGEINFO
	 *   RCX = physical address of the destination EPC page
	 */
	ret = __encls_3(SGX_EADD,
			(unsigned long)pageinfo,
			epc_phys,
			0);
	if (ret) {
		pr_debug("straylight-enclave: EADD failed 0x%x "
			 "(offset=0x%llx)\n", ret, offset);
		ret = -EIO;
		goto out;
	}

	/*
	 * ENCLS[EEXTEND]: extend measurement over each 256-byte chunk.
	 *   RCX = linear address of 256-byte chunk within the EPC page.
	 */
	for (chunk = 0; chunk < PAGE_SIZE; chunk += 256) {
		ret = __encls_1(SGX_EEXTEND,
				epc_phys + chunk);
		if (ret) {
			pr_debug("straylight-enclave: EEXTEND failed 0x%x\n",
				 ret);
			ret = -EIO;
			goto out;
		}
	}

	/* Record the page in the enclave's page table */
	page_idx = enc->nr_pages;
	enc->epc_pages[page_idx] = epc_page;
	enc->epc_phys[page_idx]  = epc_phys;
	enc->nr_pages++;
	epc_page = NULL;  /* now owned by enc */

out:
	kfree(pageinfo);
	kfree(secinfo);
	if (epc_page)
		sl_epc_free_page(NULL, epc_page);
	return ret;
}

/* ---- EINIT ------------------------------------------------------------ */

/*
 * sl_epc_einit — finalise an enclave after all pages have been added.
 *
 * EINIT verifies the SIGSTRUCT RSA signature against the accumulated
 * MRENCLAVE measurement, checks the launch token (or ignores it on
 * FLC-enabled platforms), and transitions the SECS to the INITIALIZED
 * state.  After EINIT succeeds the enclave can be entered via EENTER.
 *
 *   EINIT(EAX=2, RBX=sigstruct_va, RCX=secs_epc_pa, RDX=token_va)
 *
 * sigstruct: 1808-byte structure (4-KiB alignment strongly recommended)
 * token:     304-byte EINITTOKEN, or all-zeros on FLC platforms
 */
int sl_epc_einit(struct sl_sgx_enclave *enc,
		 void *sigstruct, void *token)
{
	u8 *zero_token = NULL;
	int ret;

	/* Use a zero token if none supplied (FLC / launch-control flow) */
	if (!token) {
		zero_token = kzalloc(304, GFP_KERNEL);
		if (!zero_token)
			return -ENOMEM;
		token = zero_token;
	}

	/*
	 * ENCLS[EINIT]:
	 *   RBX = linear address of SIGSTRUCT
	 *   RCX = physical address of the SECS EPC page
	 *   RDX = linear address of EINITTOKEN
	 */
	ret = __encls_3(SGX_EINIT,
			(unsigned long)sigstruct,
			enc->secs_phys,
			(unsigned long)token);

	kfree(zero_token);

	if (ret) {
		pr_warn("straylight-enclave: EINIT failed SGX error=0x%x "
			"(enc id=%u)\n", ret, enc->id);
		return -EIO;
	}

	pr_info("straylight-enclave: enclave %u EINIT OK\n", enc->id);
	return 0;
}
