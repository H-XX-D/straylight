// SPDX-License-Identifier: GPL-2.0
/*
 * StrayLight OS — SGX Enclave Manager, Module Entry Point
 * Copyright (C) 2026 StrayLight Systems
 *
 * Implements module_init / module_exit and a misc-device
 * /dev/straylight-sgx that userspace opens to manage enclave
 * lifecycle via ioctl.
 *
 * ioctl dispatch:
 *   SL_SGX_IOC_CREATE    — allocate SECS, run ECREATE
 *   SL_SGX_IOC_ADD_PAGE  — EADD + EEXTEND a page into the enclave
 *   SL_SGX_IOC_INIT      — EINIT (requires SIGSTRUCT + launch token)
 *   SL_SGX_IOC_SEAL      — seal plaintext via EGETKEY inside enclave
 *   SL_SGX_IOC_UNSEAL    — unseal a sealed blob
 *   SL_SGX_IOC_REPORT    — generate a local attestation REPORT
 *
 * The module loads even when SGX is absent so that userspace tooling can
 * query the device and report a graceful "SGX unavailable" error rather
 * than receiving ENOENT on the device node.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/mutex.h>

#include "enclave.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("StrayLight Systems");
MODULE_DESCRIPTION("StrayLight SGX Enclave Management");
MODULE_VERSION("1.0.0");

/* ---- ioctl command definitions ---------------------------------------- */

#define SL_SGX_IOC_MAGIC        'S'

#define SL_SGX_IOC_CREATE       _IOWR(SL_SGX_IOC_MAGIC, 1, \
					struct sl_sgx_create)
#define SL_SGX_IOC_ADD_PAGE     _IOW(SL_SGX_IOC_MAGIC,  2, \
					struct sl_sgx_add_page)
#define SL_SGX_IOC_INIT         _IOW(SL_SGX_IOC_MAGIC,  3, \
					struct sl_sgx_init_param)
#define SL_SGX_IOC_SEAL         _IOWR(SL_SGX_IOC_MAGIC, 4, \
					struct sl_sgx_sealed)
#define SL_SGX_IOC_UNSEAL       _IOWR(SL_SGX_IOC_MAGIC, 5, \
					struct sl_sgx_sealed)
#define SL_SGX_IOC_REPORT       _IOWR(SL_SGX_IOC_MAGIC, 6, \
					struct sl_sgx_report)

/* ---- Module-global state ---------------------------------------------- */

static bool sgx_present;   /* true when CPU supports SGX */

/* ---- File operations -------------------------------------------------- */

static int sl_sgx_open(struct inode *inode, struct file *filp)
{
	return 0;
}

static int sl_sgx_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static long sl_sgx_ioctl(struct file *filp, unsigned int cmd,
			 unsigned long arg)
{
	/*
	 * Non-SGX operations (query, etc.) can proceed on any hardware.
	 * Enclave lifecycle operations require SGX to be present.
	 */
	switch (cmd) {
	case SL_SGX_IOC_CREATE:
		if (!sgx_present)
			return -ENODEV;
		return sl_epc_create((struct sl_sgx_create __user *)arg);

	case SL_SGX_IOC_ADD_PAGE:
		if (!sgx_present)
			return -ENODEV;
		return sl_epc_add_page((struct sl_sgx_add_page __user *)arg);

	case SL_SGX_IOC_INIT:
		if (!sgx_present)
			return -ENODEV;
		return sl_epc_init_enclave(
				(struct sl_sgx_init_param __user *)arg);

	case SL_SGX_IOC_SEAL:
		if (!sgx_present)
			return -ENODEV;
		return sl_sealed_write((struct sl_sgx_sealed __user *)arg);

	case SL_SGX_IOC_UNSEAL:
		if (!sgx_present)
			return -ENODEV;
		return sl_sealed_read((struct sl_sgx_sealed __user *)arg);

	case SL_SGX_IOC_REPORT:
		if (!sgx_present)
			return -ENODEV;
		return sl_attestation_report(
				(struct sl_sgx_report __user *)arg);

	default:
		return -ENOTTY;
	}
}

static const struct file_operations sl_sgx_fops = {
	.owner          = THIS_MODULE,
	.open           = sl_sgx_open,
	.release        = sl_sgx_release,
	.unlocked_ioctl = sl_sgx_ioctl,
	.compat_ioctl   = compat_ptr_ioctl,
};

static struct miscdevice sl_sgx_miscdev = {
	.minor  = MISC_DYNAMIC_MINOR,
	.name   = "straylight-sgx",
	.fops   = &sl_sgx_fops,
	.mode   = 0660,
};

/* ---- Module init / exit ----------------------------------------------- */

static int __init sl_enclave_init(void)
{
	int ret;

	sgx_present = sl_epc_detect_sgx();

	if (sgx_present) {
		ret = sl_epc_init();
		if (ret) {
			pr_err("straylight-enclave: EPC init failed (%d)\n",
			       ret);
			return ret;
		}
	} else {
		pr_info("straylight-enclave: SGX not available — "
			"device node registered in degraded mode\n");
	}

	ret = misc_register(&sl_sgx_miscdev);
	if (ret) {
		pr_err("straylight-enclave: misc_register failed (%d)\n",
		       ret);
		if (sgx_present)
			sl_epc_cleanup();
		return ret;
	}

	pr_info("straylight-enclave: /dev/straylight-sgx registered "
		"(SGX %s)\n", sgx_present ? "present" : "not present");
	return 0;
}

static void __exit sl_enclave_exit(void)
{
	misc_deregister(&sl_sgx_miscdev);

	if (sgx_present)
		sl_epc_cleanup();

	pr_info("straylight-enclave: module unloaded\n");
}

module_init(sl_enclave_init);
module_exit(sl_enclave_exit);
