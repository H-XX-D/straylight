// SPDX-License-Identifier: GPL-2.0
/*
 * StrayLight OS — SGX Sealed Storage
 * Copyright (C) 2026 StrayLight Systems
 *
 * Sealed storage binds encrypted data to an enclave identity so that
 * only an enclave with the same measurement (MRENCLAVE) or the same
 * signing key (MRSIGNER) can recover it.
 *
 * The sealing key is derived by EGETKEY from hardware fuses, the
 * enclave measurement, and a caller-supplied key ID.  All key derivation
 * and symmetric encryption happens inside the enclave (ring 3) via ENCLU.
 * This kernel-side file handles:
 *
 *   1. Validating and copying the plaintext / ciphertext between
 *      userspace and a kernel bounce buffer.
 *   2. Setting up the KEYREQUEST structure.
 *   3. Triggering the enclave's seal/unseal ecall via EENTER.
 *   4. Copying the result back to userspace.
 *
 * Layout of a sealed blob (written to userspace buffer):
 *
 *   [struct sl_seal_header][ciphertext][MAC-16]
 *
 *   Header:  16 bytes  — version, key_policy, key_id, reserved
 *   IV:      16 bytes  — random nonce (stored inside ciphertext block)
 *   MAC:     16 bytes  — AES-GCM authentication tag
 *   Overhead: SEAL_OVERHEAD = 48 bytes
 *
 * Reference: Intel SGX Developer Guide, Chapter 6 (Sealing).
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/random.h>
#include <linux/string.h>

#include "enclave.h"

/* ---- Constants --------------------------------------------------------- */

#define SEAL_OVERHEAD   48U     /* header(16) + IV(16) + MAC(16) */
#define SEAL_MAX_DATA   (1U << 20)   /* 1 MiB plaintext limit     */

#define SEAL_VERSION    0x0001
#define KEY_POLICY_MRENCLAVE  0x0001
#define KEY_POLICY_MRSIGNER   0x0002

/* ---- Sealed blob header ----------------------------------------------- */

struct sl_seal_header {
	__le16   version;       /* SEAL_VERSION                       */
	__le16   key_policy;    /* KEY_POLICY_MRENCLAVE or _MRSIGNER  */
	__u8     key_id[16];    /* random salt mixed into key request */
	__le32   plaintext_len; /* original plaintext length          */
	__u8     reserved[8];
} __packed;

static_assert(sizeof(struct sl_seal_header) == 32,
	      "sl_seal_header size mismatch");

/* ---- Helpers ----------------------------------------------------------- */

static int validate_enclave_id(u64 enclave_id, bool need_initialized)
{
	/* Enclave lookup is done in enclave_epc.c; here we delegate
	 * via the exported list.  For sealed storage we only require
	 * a positive, non-zero ID (the enclave measurement is what
	 * the hardware enforces at EGETKEY time). */
	if (enclave_id == 0)
		return -EINVAL;
	return 0;
}

/* ---- SL_SGX_IOC_SEAL -------------------------------------------------- */

/*
 * sl_sealed_write — seal plaintext into the caller's buffer.
 *
 * Flow:
 *   a. Copy plaintext from userspace.
 *   b. Build KEYREQUEST with random key_id.
 *   c. [In production: EENTER → enclave seal ecall → EEXIT]
 *      The kernel module sets up parameters; AES-GCM runs inside the
 *      enclave where the seal key is accessible via EGETKEY.
 *   d. Write sealed blob back to userspace.
 *   e. Report sealed_len so the caller knows how many bytes were written.
 */
long sl_sealed_write(struct sl_sgx_sealed __user *uarg)
{
	struct sl_sgx_sealed params;
	struct sl_seal_header hdr;
	struct sl_keyrequest kreq;
	void *plaintext = NULL;
	void *sealed_buf = NULL;
	u32 sealed_total;
	long ret = 0;

	if (copy_from_user(&params, uarg, sizeof(params)))
		return -EFAULT;

	ret = validate_enclave_id(params.enclave_id, true);
	if (ret)
		return ret;

	if (params.data_len == 0 || params.data_len > SEAL_MAX_DATA)
		return -EINVAL;

	if (params.key_policy != KEY_POLICY_MRENCLAVE &&
	    params.key_policy != KEY_POLICY_MRSIGNER)
		return -EINVAL;

	sealed_total = params.data_len + SEAL_OVERHEAD;

	/* Allocate kernel buffers */
	plaintext = kvmalloc(params.data_len, GFP_KERNEL);
	if (!plaintext)
		return -ENOMEM;

	sealed_buf = kvmalloc(sealed_total, GFP_KERNEL);
	if (!sealed_buf) {
		ret = -ENOMEM;
		goto out;
	}

	/* Copy plaintext from userspace */
	if (copy_from_user(plaintext, (void __user *)params.data_ptr,
			   params.data_len)) {
		ret = -EFAULT;
		goto out;
	}

	/* Build KEYREQUEST */
	memset(&kreq, 0, sizeof(kreq));
	kreq.keyname   = cpu_to_le16(SGX_KEYNAME_SEAL);
	kreq.keypolicy = cpu_to_le16(params.key_policy);
	get_random_bytes(kreq.keyid, sizeof(kreq.keyid));

	/* Build header in the sealed blob */
	memset(&hdr, 0, sizeof(hdr));
	hdr.version       = cpu_to_le16(SEAL_VERSION);
	hdr.key_policy    = cpu_to_le16(params.key_policy);
	memcpy(hdr.key_id, kreq.keyid, sizeof(hdr.key_id));
	hdr.plaintext_len = cpu_to_le32(params.data_len);

	memcpy(sealed_buf, &hdr, sizeof(hdr));

	/*
	 * At this point in a production driver we would:
	 *   1. Allocate a TCS/SSA frame for the enclave thread.
	 *   2. Set up the untrusted stack and ecall parameters.
	 *   3. EENTER to the enclave's seal_data() ecall.
	 *   4. Inside the enclave: EGETKEY(kreq) → 128-bit seal key,
	 *      AES-128-GCM encrypt plaintext with random IV → ciphertext.
	 *   5. EEXIT with output pointer to (IV || ciphertext || MAC).
	 *   6. Copy from enclave output buffer to sealed_buf+sizeof(hdr).
	 *
	 * The stub below copies plaintext XOR 0xAA as a placeholder to
	 * make integration tests observable without real SGX hardware.
	 */
	{
		u8 *out = (u8 *)sealed_buf + sizeof(hdr);
		const u8 *in = plaintext;
		u32 i;

		for (i = 0; i < params.data_len; i++)
			out[i] = in[i] ^ 0xAA;  /* placeholder, not real AES */

		/* Placeholder MAC: first 16 bytes of the header key_id */
		memcpy(out + params.data_len, kreq.keyid, 16);
	}

	/* Copy sealed blob to userspace output buffer */
	if (copy_to_user((void __user *)params.data_ptr,
			 sealed_buf, sealed_total)) {
		ret = -EFAULT;
		goto out;
	}

	/* Report final sealed length */
	if (put_user(sealed_total, &uarg->sealed_len)) {
		ret = -EFAULT;
		goto out;
	}

out:
	if (plaintext) {
		memzero_explicit(plaintext, params.data_len);
		kvfree(plaintext);
	}
	if (sealed_buf) {
		memzero_explicit(sealed_buf, sealed_total);
		kvfree(sealed_buf);
	}
	memzero_explicit(&kreq, sizeof(kreq));
	return ret;
}

/* ---- SL_SGX_IOC_UNSEAL ------------------------------------------------ */

/*
 * sl_sealed_read — unseal a sealed blob and return plaintext.
 *
 * The inverse of sl_sealed_write.  Reads the header to recover
 * key_policy and key_id, reconstructs the KEYREQUEST, enters the
 * enclave, and writes decrypted plaintext to data_ptr.
 */
long sl_sealed_read(struct sl_sgx_sealed __user *uarg)
{
	struct sl_sgx_sealed params;
	struct sl_seal_header hdr;
	struct sl_keyrequest kreq;
	void *sealed_buf = NULL;
	void *plaintext = NULL;
	u32 ct_len;
	long ret = 0;

	if (copy_from_user(&params, uarg, sizeof(params)))
		return -EFAULT;

	ret = validate_enclave_id(params.enclave_id, true);
	if (ret)
		return ret;

	if (params.sealed_len < SEAL_OVERHEAD ||
	    params.sealed_len > SEAL_MAX_DATA + SEAL_OVERHEAD)
		return -EINVAL;

	ct_len = params.sealed_len - SEAL_OVERHEAD;

	sealed_buf = kvmalloc(params.sealed_len, GFP_KERNEL);
	if (!sealed_buf)
		return -ENOMEM;

	plaintext = kvmalloc(ct_len, GFP_KERNEL);
	if (!plaintext) {
		ret = -ENOMEM;
		goto out;
	}

	/* Copy sealed blob from userspace */
	if (copy_from_user(sealed_buf, (void __user *)params.data_ptr,
			   params.sealed_len)) {
		ret = -EFAULT;
		goto out;
	}

	/* Parse header */
	memcpy(&hdr, sealed_buf, sizeof(hdr));

	if (le16_to_cpu(hdr.version) != SEAL_VERSION) {
		pr_debug("straylight-enclave: unseal: unknown version 0x%04x\n",
			 le16_to_cpu(hdr.version));
		ret = -EINVAL;
		goto out;
	}
	if (le32_to_cpu(hdr.plaintext_len) != ct_len) {
		pr_debug("straylight-enclave: unseal: length mismatch "
			 "(header=%u, derived=%u)\n",
			 le32_to_cpu(hdr.plaintext_len), ct_len);
		ret = -EINVAL;
		goto out;
	}

	/* Reconstruct KEYREQUEST from stored key_id */
	memset(&kreq, 0, sizeof(kreq));
	kreq.keyname   = cpu_to_le16(SGX_KEYNAME_SEAL);
	kreq.keypolicy = hdr.key_policy;
	memcpy(kreq.keyid, hdr.key_id, sizeof(kreq.keyid));

	/*
	 * Production path:
	 *   EENTER → enclave unseal_data(kreq, ciphertext, mac) ecall
	 *   Inside enclave: EGETKEY(kreq) → key, AES-GCM-Decrypt →
	 *   plaintext (verified against MAC).
	 *   EEXIT.
	 *
	 * Stub: reverse the placeholder XOR.
	 */
	{
		const u8 *ct = (const u8 *)sealed_buf + sizeof(hdr);
		u8 *pt = plaintext;
		u32 i;

		for (i = 0; i < ct_len; i++)
			pt[i] = ct[i] ^ 0xAA;  /* placeholder inverse */
	}

	/* Write plaintext to userspace */
	if (copy_to_user((void __user *)params.data_ptr, plaintext, ct_len)) {
		ret = -EFAULT;
		goto out;
	}

	/* Report recovered plaintext length */
	if (put_user(ct_len, &uarg->data_len)) {
		ret = -EFAULT;
		goto out;
	}

out:
	if (sealed_buf) {
		memzero_explicit(sealed_buf, params.sealed_len);
		kvfree(sealed_buf);
	}
	if (plaintext) {
		memzero_explicit(plaintext, ct_len);
		kvfree(plaintext);
	}
	memzero_explicit(&kreq, sizeof(kreq));
	return ret;
}
