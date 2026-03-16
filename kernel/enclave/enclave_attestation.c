// SPDX-License-Identifier: GPL-2.0
/*
 * StrayLight OS — SGX Local Attestation
 * Copyright (C) 2026 StrayLight Systems
 *
 * Local attestation lets one enclave on the same platform prove its
 * identity to another without going off-platform.  The sequence is:
 *
 *   1. Verifier (B) reads its own TARGETINFO from its SECS.
 *   2. Prover  (A) calls EREPORT(TARGETINFO_B, reportdata) → REPORT.
 *      The CPU signs REPORT with a key derived from platform secrets
 *      and the enclave measurement.  Only B can verify it via EGETKEY.
 *   3. Prover returns REPORT to verifier (via this kernel ioctl).
 *   4. Verifier calls EGETKEY(REPORT_KEY) to derive the report MAC key,
 *      verifies REPORT.MAC, and checks MRENCLAVE/MRSIGNER/attributes.
 *
 * EREPORT is an ENCLU instruction (ring 3 only).  The kernel module:
 *   a. Validates the enclave handle.
 *   b. Copies TARGETINFO and REPORTDATA from userspace into kernel
 *      bounce buffers.
 *   c. Schedules EENTER into the enclave's report_ecall() handler which
 *      executes EREPORT inside the enclave.
 *   d. Copies the 432-byte REPORT back to userspace.
 *
 * Reference: Intel SDM Vol. 3D §38.4 (EREPORT), §40.1.4 (local attest).
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/string.h>

#include "enclave.h"

/* ---- Report MAC stub --------------------------------------------------- */

/*
 * On real SGX hardware the REPORT.MAC is a CMAC-AES-128 computed by the
 * CPU microcode over the REPORT body using a key known only to the target
 * enclave (retrieved via EGETKEY(REPORT_KEY)).  The prover's enclave
 * executes EREPORT; the resulting REPORT structure is the deliverable.
 *
 * Here we fill the MAC with a deterministic placeholder so that
 * integration tests can parse the structure without real hardware.
 */
static void fill_report_mac(struct sl_report *report)
{
	/*
	 * Placeholder: XOR-fold MRENCLAVE into a 16-byte MAC slot.
	 * A production enclave computes this with the hardware key via EREPORT.
	 */
	int i;

	for (i = 0; i < 16; i++)
		report->mac[i] = report->mrenclave[i] ^
				 report->mrenclave[i + 16];
}

/* ---- SL_SGX_IOC_REPORT ------------------------------------------------ */

/*
 * sl_attestation_report — generate a local attestation REPORT.
 *
 * @uarg:  pointer to sl_sgx_report in userspace.
 *         .enclave_id   — prover enclave handle
 *         .targetinfo   — 512-byte TARGETINFO of the verifying enclave
 *         .reportdata   — 64-byte user-defined nonce / public key hash
 *         .report       — 432-byte output REPORT (filled by this call)
 *
 * Returns 0 on success, negative errno on failure.
 */
long sl_attestation_report(struct sl_sgx_report __user *uarg)
{
	struct sl_sgx_report *args;
	struct sl_targetinfo *ti;
	struct sl_reportdata *rd;
	struct sl_report *report;
	long ret = 0;

	/*
	 * Allocate one heap block for all three sub-structures.
	 * All must be naturally aligned (TARGETINFO: 512 B, REPORT: 512 B).
	 * Using a single allocation simplifies cleanup.
	 */
	args = kzalloc(sizeof(*args), GFP_KERNEL);
	if (!args)
		return -ENOMEM;

	if (copy_from_user(args, uarg, sizeof(*args))) {
		ret = -EFAULT;
		goto out;
	}

	/* Validate the enclave handle */
	if (args->enclave_id == 0) {
		ret = -EINVAL;
		goto out;
	}

	/* Lay pointers over the embedded arrays */
	ti     = (struct sl_targetinfo *)args->targetinfo;
	rd     = (struct sl_reportdata *)args->reportdata;
	report = (struct sl_report *)args->report;

	/*
	 * --- Production execution path ---
	 *
	 * The prover enclave must execute EREPORT (an ENCLU instruction,
	 * ring-3 only) with:
	 *   RBX = linear address of TARGETINFO (in enclave virtual space)
	 *   RCX = linear address of REPORTDATA (64 bytes)
	 *   RDX = linear address of output REPORT buffer (512 bytes)
	 *
	 * The kernel module triggers this via EENTER into the enclave's
	 * dedicated report_ecall() entry point.  The enclave maps the
	 * TARGETINFO and REPORTDATA from the shared memory region, executes
	 * EREPORT, then EEXIT.  The completed REPORT is written to the
	 * shared output buffer, which the kernel then copies here.
	 *
	 * Steps (abbreviated for clarity):
	 *   1. sl_epc_find(args->enclave_id)   — get struct sl_enclave *
	 *   2. map_shared_page(enc, &ti, &rd)   — copy inputs into enclave VA
	 *   3. __enclu(SGX_EENTER, tcs_pa, aep, &gpr_state) — enter enclave
	 *      [enclave executes EREPORT then EEXIT]
	 *   4. copy report from shared page
	 *
	 * --- Stub path (no SGX hardware) ---
	 *
	 * We populate the report fields synthetically so that userspace
	 * can exercise the complete ioctl path on non-SGX machines.
	 */

	/* Zero the output REPORT */
	memset(report, 0, sizeof(*report));

	/*
	 * Populate synthetic REPORT fields.
	 * cpusvn_and_policy carries a copy of TARGETINFO (as a proxy for
	 * what the CPU would embed from platform state).
	 */
	memcpy(&report->cpusvn_and_policy, ti, sizeof(*ti));

	/* Embed the caller's nonce in reportdata */
	memcpy(report->reportdata, rd->data, sizeof(rd->data));

	/*
	 * MRENCLAVE: on real hardware this is the running enclave's SHA-256
	 * measurement, accumulated during EADD/EEXTEND.  Use the enclave_id
	 * as a distinguishable placeholder.
	 */
	{
		u64 id = args->enclave_id;

		memcpy(report->mrenclave, &id, sizeof(id));
	}

	/* MRSIGNER: SHA-256 of the signing key — zero for an unsigned stub */
	memset(report->mrsigner, 0, sizeof(report->mrsigner));

	/* Derive the placeholder MAC */
	fill_report_mac(report);

	/* Copy the completed REPORT back to userspace */
	if (copy_to_user(uarg->report, report, sizeof(*report))) {
		ret = -EFAULT;
		goto out;
	}

out:
	/* The REPORT contains sensitive measurement data — zeroize on error */
	if (ret)
		memzero_explicit(args, sizeof(*args));

	kfree(args);
	return ret;
}
