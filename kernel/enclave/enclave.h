/* SPDX-License-Identifier: GPL-2.0 */
/*
 * StrayLight OS — SGX Enclave Management, Internal Header
 * Copyright (C) 2026 StrayLight Systems
 *
 * Shared definitions used by enclave_main.c, enclave_epc.c,
 * enclave_sealed.c, and enclave_attestation.c.
 */

#ifndef _STRAYLIGHT_ENCLAVE_H
#define _STRAYLIGHT_ENCLAVE_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/atomic.h>

/* ---- SGX ENCLS leaf opcodes ------------------------------------------- */

#define SGX_ECREATE     0x00
#define SGX_EADD        0x01
#define SGX_EINIT       0x02
#define SGX_EREMOVE     0x03
#define SGX_EDBGRD      0x04
#define SGX_EDBGWR      0x05
#define SGX_EEXTEND     0x06
#define SGX_ELDB        0x07
#define SGX_ELDU        0x08
#define SGX_EBLOCK      0x09
#define SGX_EPA         0x0A
#define SGX_EWB         0x0B
#define SGX_ETRACK      0x0C

/* ---- SGX ENCLU leaf opcodes (ring-3 only) ------------------------------ */

#define SGX_EREPORT     0x00
#define SGX_EGETKEY     0x01
#define SGX_EENTER      0x02
#define SGX_ERESUME     0x03
#define SGX_EEXIT       0x04
#define SGX_EACCEPT     0x05
#define SGX_EMODPE      0x06
#define SGX_EACCEPTCOPY 0x07

/* ---- Page types -------------------------------------------------------- */

#define SGX_PAGE_TYPE_SECS  0x00
#define SGX_PAGE_TYPE_TCS   0x01
#define SGX_PAGE_TYPE_REG   0x02
#define SGX_PAGE_TYPE_VA    0x03
#define SGX_PAGE_TYPE_TRIM  0x04

/* ---- SGX SECS (Enclave Control Structure) ------------------------------ */

/*
 * Full SECS is 4096 bytes, hardware-managed.  We track only the fields
 * we write at ECREATE time.
 */
struct sl_secs {
	u64  size;              /* enclave virtual size (power of 2) */
	u64  base;              /* enclave base address              */
	u32  ssa_frame_size;    /* SSA frame size in pages           */
	u32  miscselect;        /* extended features the CPU saves   */
	u8   reserved1[24];
	u64  attributes;        /* XFRM and attribute flags          */
	u8   mrsigner[32];      /* SHA-256 of signer's public key    */
	u8   reserved2[96];
	u16  isvprodid;         /* ISV product ID                    */
	u16  isvsvn;            /* ISV security version number       */
} __packed;

/* ---- SGX SECINFO (page properties, passed to EADD) -------------------- */

struct sl_secinfo {
	u64  flags;             /* page type and permissions         */
	u8   reserved[56];
} __packed;

#define SECINFO_R       (1ULL << 0)
#define SECINFO_W       (1ULL << 1)
#define SECINFO_X       (1ULL << 2)
#define SECINFO_PT_MASK (0xFFULL << 8)
#define SECINFO_PT_TCS  (SGX_PAGE_TYPE_TCS  << 8)
#define SECINFO_PT_REG  (SGX_PAGE_TYPE_REG  << 8)

/* ---- SGX SIGSTRUCT (4096-byte signing structure) ----------------------- */

#define SGX_SIGSTRUCT_SIZE      1808
#define SGX_EINITTOKEN_SIZE     304

/* ---- TARGETINFO (used by EREPORT to bind report to verifier) ----------- */

struct sl_targetinfo {
	u8   measurement[32];   /* MRENCLAVE of the target enclave   */
	u64  attributes;
	u32  miscselect;
	u8   reserved[456];
} __packed;

/* ---- REPORTDATA (64-byte caller-supplied nonce/context) ---------------- */

struct sl_reportdata {
	u8   data[64];
} __packed;

/* ---- REPORT (432-byte structure produced by EREPORT) ------------------ */

struct sl_report {
	struct sl_targetinfo cpusvn_and_policy;  /* simplified */
	u8   reportdata[64];
	u8   mrenclave[32];
	u8   mrsigner[32];
	u64  attributes;
	u32  miscselect;
	u16  isvprodid;
	u16  isvsvn;
	u8   mac[16];
	u8   reserved[96];
} __packed;

/* ---- KEYREQUEST (used by EGETKEY to select the key type) --------------- */

#define SGX_KEYNAME_SEAL        0x0004

struct sl_keyrequest {
	u16  keyname;           /* SGX_KEYNAME_SEAL                  */
	u16  keypolicy;         /* MRENCLAVE=0x01, MRSIGNER=0x02     */
	u16  isvsvn;
	u16  reserved1;
	u8   cpusvn[16];
	u64  attributemask;
	u8   keyid[32];
	u32  miscmask;
	u8   reserved2[20];
} __packed;

/* ---- Internal enclave descriptor --------------------------------------- */

#define SL_MAX_ENCLAVES         64

struct sl_enclave {
	struct list_head        list;
	u64                     id;             /* kernel-assigned handle */
	u64                     base;           /* virtual base address   */
	u64                     size;
	u64                     attributes;
	bool                    initialized;
	atomic_t                refcount;
};

/* ---- ioctl argument structures (shared kernel ↔ userspace) ------------ */

struct sl_sgx_create {
	__u64  base_addr;       /* in:  enclave base (page-aligned)  */
	__u64  size;            /* in:  enclave size (power of 2)    */
	__u64  secs_attr;       /* in:  SECS attribute flags         */
	__u64  enclave_id;      /* out: opaque handle                */
};

struct sl_sgx_add_page {
	__u64  enclave_id;      /* in:  enclave handle from CREATE   */
	__u64  src_addr;        /* in:  userspace source page VA     */
	__u64  offset;          /* in:  byte offset within enclave   */
	__u64  secinfo_flags;   /* in:  SECINFO flags (type + perms) */
};

struct sl_sgx_init_param {
	__u64  enclave_id;
	__u8   sigstruct[SGX_SIGSTRUCT_SIZE];
	__u8   einittoken[SGX_EINITTOKEN_SIZE];
};

struct sl_sgx_sealed {
	__u64  enclave_id;
	__u64  data_ptr;        /* userspace buffer VA               */
	__u32  data_len;        /* length of plaintext / ciphertext  */
	__u32  sealed_len;      /* out: bytes written to data_ptr    */
	__u16  key_policy;      /* MRENCLAVE=0x01, MRSIGNER=0x02     */
	__u16  reserved;
};

struct sl_sgx_report {
	__u64  enclave_id;
	__u8   targetinfo[512]; /* TARGETINFO of verifying enclave   */
	__u8   reportdata[64];  /* nonce / user data bound to report */
	__u8   report[432];     /* out: REPORT structure             */
};

/* ---- Function declarations -------------------------------------------- */

/* enclave_epc.c */
bool  sl_epc_detect_sgx(void);
int   sl_epc_init(void);
void  sl_epc_cleanup(void);
long  sl_epc_create(struct sl_sgx_create __user *arg);
long  sl_epc_add_page(struct sl_sgx_add_page __user *arg);
long  sl_epc_init_enclave(struct sl_sgx_init_param __user *arg);

/* enclave_sealed.c */
long  sl_sealed_write(struct sl_sgx_sealed __user *arg);
long  sl_sealed_read(struct sl_sgx_sealed __user *arg);

/* enclave_attestation.c */
long  sl_attestation_report(struct sl_sgx_report __user *arg);

#endif /* _STRAYLIGHT_ENCLAVE_H */
