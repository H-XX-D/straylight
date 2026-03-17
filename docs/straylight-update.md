# straylight-update

## NAME

straylight-update -- atomic system updater with A/B partition support

## SYNOPSIS

```
straylight-update [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-update manages system updates for StrayLight OS using an A/B partition scheme. Updates are downloaded and applied to the inactive partition while the system continues running on the active partition. On reboot, the system switches to the updated partition. If the update fails to boot, the system automatically rolls back to the previous partition.

This approach guarantees that a failed update can never brick the system. The currently running system is never modified during the update process, eliminating the risk of partial updates or interrupted package installations leaving the system in an inconsistent state.

straylight-update also manages application and driver updates through a conventional package manager interface, but wraps each transaction in a straylight-snapshot checkpoint so that any update can be rolled back individually.

## COMMANDS

### `check`

Check for available updates.

```
straylight-update check [--json]
```

### `apply`

Download and apply updates.

```
straylight-update apply [--reboot] [--security-only]
```

### `rollback`

Rollback to the previous system version.

```
straylight-update rollback [--confirm]
```

### `status`

Show current update status and partition layout.

```
straylight-update status [--json]
```

### `history`

Show update history.

```
straylight-update history [--last <n>]
```

### `hold`

Hold a package at its current version.

```
straylight-update hold <package>
```

### `unhold`

Remove a package hold.

```
straylight-update unhold <package>
```

### `channel`

Manage update channels.

```
straylight-update channel list
straylight-update channel set <stable|testing|unstable>
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--reboot` | Reboot after applying updates |
| `--security-only` | Only apply security updates |
| `--dry-run` | Check what would be updated |
| `--json` | JSON output |
| `--verbose` | Debug logging |

## CONFIGURATION

```toml
[update]
channel = "stable"
auto_check = true
check_interval = "6h"
auto_apply = false
auto_reboot = false

[ab_partition]
active = "A"
fallback_timeout = 120      # seconds to wait before rolling back

[snapshot]
pre_update = true
retention = 5

[mirrors]
urls = ["https://update.straylight.os/stable"]
```

## EXAMPLES

Check for updates:

```
straylight-update check
```

Apply all updates and reboot:

```
straylight-update apply --reboot
```

Apply security updates only:

```
straylight-update apply --security-only
```

Rollback the last update:

```
straylight-update rollback --confirm
```

Hold a package:

```
straylight-update hold nvidia-driver
```

Switch to testing channel:

```
straylight-update channel set testing
```

## INTEGRATION

- **straylight-snapshot**: Pre-update snapshots are created automatically.
- **straylight-boot**: Boot configuration is updated for A/B partition switching.
- **straylight-echo**: Update transactions are registered for system-wide undo.
- **straylight-hotpatch**: Critical fixes can be applied live without full updates.
- **straylight-shield**: Security updates are prioritized based on shield's vulnerability scan.
- **straylight-notify**: Update availability and completion are notified.

## SEE ALSO

straylight-boot(1), straylight-snapshot(1), straylight-hotpatch(1), straylight-shield(1), straylight-echo(1)
