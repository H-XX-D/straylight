# straylight-boot

## NAME

straylight-boot -- boot manager for kernel selection, initramfs, and boot-environment rollback

## SYNOPSIS

```
straylight-boot [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-boot manages the boot process for StrayLight OS. It configures the bootloader (systemd-boot or GRUB), manages kernel installations, generates and updates initramfs images, and provides boot environment management with automatic rollback on failure.

boot integrates with straylight-update's A/B partition scheme to ensure the system always has a known-good boot environment to fall back to. When a new kernel is installed, boot creates a boot entry and marks it as "tentative." If the system fails to reach a healthy state within a configurable timeout after booting the new kernel, it automatically reboots into the previous working kernel.

The tool also manages kernel command-line parameters, secure boot enrollment, and boot-time service ordering. It provides a repair mode accessible from the bootloader for systems that cannot reach userspace.

## COMMANDS

### `list`

List boot entries and environments.

```
straylight-boot list [--json] [--verbose]
```

### `default`

Get or set the default boot entry.

```
straylight-boot default [<entry-id>]
```

### `add`

Add a boot entry.

```
straylight-boot add --kernel <path> --initrd <path> [--cmdline <params>] [--label <name>]
```

### `remove`

Remove a boot entry.

```
straylight-boot remove <entry-id>
```

### `initramfs`

Generate or rebuild the initramfs.

```
straylight-boot initramfs rebuild [--kernel <version>]
```

### `cmdline`

Manage kernel command-line parameters.

```
straylight-boot cmdline show
straylight-boot cmdline add <param>
straylight-boot cmdline remove <param>
```

### `rollback`

Boot into the previous working environment.

```
straylight-boot rollback [--confirm]
```

### `repair`

Enter repair mode on next boot.

```
straylight-boot repair
```

### `secure-boot`

Manage Secure Boot keys.

```
straylight-boot secure-boot status
straylight-boot secure-boot enroll <key-path>
```

### `status`

Show current boot state.

```
straylight-boot status [--json]
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--json` | JSON output |
| `--verbose` | Debug logging |

## CONFIGURATION

```toml
[boot]
bootloader = "systemd-boot"
timeout = 5
default = "latest"

[rollback]
auto_rollback = true
health_timeout = 120        # seconds to wait for healthy state
max_attempts = 3

[initramfs]
generator = "dracut"
modules = ["btrfs", "luks", "lvm"]
compress = "zstd"

[secure_boot]
enabled = true
vendor_keys = true

[cmdline]
default = "quiet splash"
```

## EXAMPLES

List boot entries:

```
straylight-boot list
```

Set a specific kernel as default:

```
straylight-boot default entry-linux-6.8.1
```

Rebuild the initramfs for the current kernel:

```
straylight-boot initramfs rebuild
```

Add a kernel parameter:

```
straylight-boot cmdline add "nvidia.NVreg_PreserveVideoMemoryAllocations=1"
```

Rollback to the previous kernel:

```
straylight-boot rollback --confirm
```

Check secure boot status:

```
straylight-boot secure-boot status
```

Enter repair mode:

```
straylight-boot repair
```

## INTEGRATION

- **straylight-update**: Boot entries are created during system updates.
- **straylight-snapshot**: Boot environments are backed by filesystem snapshots.
- **straylight-mirror**: Boot configuration is synchronized to mirror targets.
- **straylight-health**: Boot health timeout uses the health monitor.
- **straylight-echo**: Boot configuration changes are registered for undo.
- **straylight-shield**: Secure boot status is checked during security audits.

## SEE ALSO

straylight-update(1), straylight-snapshot(1), straylight-health(1), straylight-shield(1), straylight-echo(1)
