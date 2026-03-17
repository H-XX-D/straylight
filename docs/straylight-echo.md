# straylight-echo

## NAME

straylight-echo -- system-wide undo that reverts configuration and filesystem changes

## SYNOPSIS

```
straylight-echo [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-echo provides a unified undo mechanism for StrayLight OS. Every tool in the StrayLight suite registers its changes with echo, creating a chronological ledger of system modifications. When something goes wrong, echo can revert individual changes, entire tool sessions, or roll the system back to a specific point in time.

echo works by intercepting change notifications from the StrayLight IPC bus. Each change is recorded as a reversible operation: the original state is preserved alongside the new state. For filesystem changes, echo stores diffs or full file copies depending on size. For kernel parameter changes, it records the previous value. For package installations, it stores the removal instructions.

Unlike filesystem snapshots (which capture everything), echo provides surgical undo at the individual-change level. This means you can revert a single configuration file edit without affecting anything else that happened at the same time.

## COMMANDS

### `list`

Show the change history.

```
straylight-echo list [--since <datetime>] [--tool <name>] [--limit <n>]
```

### `undo`

Revert a specific change or set of changes.

```
straylight-echo undo <change-id>
straylight-echo undo --last [<n>]
straylight-echo undo --session <session-id>
```

### `redo`

Re-apply a previously undone change.

```
straylight-echo redo <change-id>
```

### `inspect`

Show the details of a specific change.

```
straylight-echo inspect <change-id> [--diff]
```

### `rollback`

Revert all changes back to a specific point in time.

```
straylight-echo rollback <timestamp> [--dry-run]
```

### `protect`

Mark a change as protected, preventing accidental undo.

```
straylight-echo protect <change-id>
```

### `gc`

Garbage-collect old change records beyond the retention window.

```
straylight-echo gc [--older-than <duration>] [--dry-run]
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--json` | JSON output |
| `--dry-run` | Show what would be undone without doing it |
| `--force` | Undo even protected changes |
| `--verbose` | Debug logging |

## CONFIGURATION

```toml
[echo]
store_path = "/var/lib/straylight/echo"
retention = "30d"
max_store_size = "10G"

[capture]
filesystem = true
sysctl = true
packages = true
services = true

[safety]
require_confirmation = true
protect_boot_changes = true
```

## EXAMPLES

Show the last 20 changes:

```
straylight-echo list --limit 20
```

Undo the last change:

```
straylight-echo undo --last
```

Undo all changes from a specific tool session:

```
straylight-echo undo --session autotune-20260315-143022
```

Preview a full rollback to yesterday:

```
straylight-echo rollback "2026-03-15 00:00:00" --dry-run
```

Inspect what a change did:

```
straylight-echo inspect chg-4821 --diff
```

Clean up records older than 60 days:

```
straylight-echo gc --older-than 60d
```

## INTEGRATION

- **straylight-autotune**: Every kernel parameter change is registered with echo.
- **straylight-update**: System updates create echo checkpoints for rollback.
- **straylight-hotpatch**: Live patches are reversible through echo.
- **straylight-snapshot**: echo can trigger a snapshot before risky rollbacks.
- **straylight-timeline**: Undo/redo actions appear in the activity timeline.

## SEE ALSO

straylight-snapshot(1), straylight-timeline(1), straylight-update(1), straylight-hotpatch(1), straylight-rewind(1)
