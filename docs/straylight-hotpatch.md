# straylight-hotpatch

## NAME

straylight-hotpatch -- live patching of running kernels and services without restart

## SYNOPSIS

```
straylight-hotpatch [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-hotpatch applies security fixes and bug patches to the running kernel and long-lived services without requiring a reboot or service restart. It uses the kernel livepatch infrastructure (ftrace-based function replacement) for kernel patches and binary rewriting with checkpoint support for userspace services.

Each hotpatch is a signed, versioned unit that contains the replacement code, metadata describing what it fixes (CVE identifiers, bug tracker references), and rollback instructions. Patches are atomic -- they either apply completely or not at all. The system maintains a stack of applied patches that can be inspected and unwound.

straylight-hotpatch integrates with straylight-update for patch delivery and with straylight-echo for rollback tracking. Critical security patches can be applied automatically when straylight-shield detects an active vulnerability.

## COMMANDS

### `apply`

Apply a hotpatch.

```
straylight-hotpatch apply <patch-file> [--force]
```

### `revert`

Revert a previously applied patch.

```
straylight-hotpatch revert <patch-id>
```

### `list`

List all applied patches.

```
straylight-hotpatch list [--json] [--kernel] [--userspace]
```

### `check`

Check if a patch is applicable to the current system.

```
straylight-hotpatch check <patch-file>
```

### `status`

Show the current patch state.

```
straylight-hotpatch status [--verbose]
```

### `fetch`

Download available patches from the update server.

```
straylight-hotpatch fetch [--security-only]
```

### `auto`

Enable or configure automatic patching.

```
straylight-hotpatch auto [--enable] [--disable] [--policy <security|all>]
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--force` | Apply even if version checks fail |
| `--dry-run` | Check applicability without applying |
| `--json` | JSON output |
| `--verbose` | Debug logging |

## CONFIGURATION

```toml
[hotpatch]
auto_apply = "security"     # none | security | all
check_interval = "1h"
patch_store = "/var/lib/straylight/hotpatch"

[verification]
require_signature = true
trusted_keys = ["/etc/straylight/keys/patch-signing.pub"]

[kernel]
enabled = true
max_patches = 50

[userspace]
enabled = true
services = ["straylight-*"]
```

## EXAMPLES

Apply a security patch:

```
straylight-hotpatch apply CVE-2026-1234.slp
```

Check what patches are available:

```
straylight-hotpatch fetch --security-only
```

List applied kernel patches:

```
straylight-hotpatch list --kernel
```

Revert a specific patch:

```
straylight-hotpatch revert CVE-2026-1234
```

Enable automatic security patching:

```
straylight-hotpatch auto --enable --policy security
```

Dry-run to check compatibility:

```
straylight-hotpatch apply kernel-perf-fix.slp --dry-run
```

## INTEGRATION

- **straylight-update**: Hotpatches are delivered through the update channel.
- **straylight-echo**: Patch applications are registered for system-wide undo.
- **straylight-shield**: Security audit identifies vulnerabilities that hotpatch can fix.
- **straylight-notify**: Notifications on patch application or failure.
- **straylight-health**: Patch state contributes to the security component of health score.

## SEE ALSO

straylight-update(1), straylight-echo(1), straylight-shield(1), straylight-health(1), straylight-boot(1)
