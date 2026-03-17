# straylight-snapshot

## NAME

straylight-snapshot -- filesystem snapshots using Btrfs/ZFS with automated retention policies

## SYNOPSIS

```
straylight-snapshot [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-snapshot manages filesystem snapshots on Btrfs and ZFS volumes. It creates instant, space-efficient point-in-time copies of filesystems that can be browsed, compared, and restored. Snapshots are the primary data protection mechanism on StrayLight OS, complementing straylight-echo's change-level undo and straylight-mirror's continuous replication.

The tool automates snapshot lifecycle with configurable retention policies. A typical policy keeps hourly snapshots for 24 hours, daily snapshots for 30 days, and weekly snapshots for 6 months. The pruning engine runs automatically and respects hold locks placed by other tools (e.g., straylight-migrate holds snapshots during transfer).

straylight-snapshot also supports sending snapshots to remote hosts for off-site backup using incremental send/receive, which transfers only the changes since the last snapshot.

## COMMANDS

### `create`

Create a snapshot.

```
straylight-snapshot create [<filesystem>] [--tag <label>] [--readonly]
```

### `list`

List snapshots.

```
straylight-snapshot list [<filesystem>] [--json] [--sort <date|size>]
```

### `restore`

Restore a filesystem from a snapshot.

```
straylight-snapshot restore <snapshot-id> [--target <path>]
```

### `delete`

Delete a snapshot.

```
straylight-snapshot delete <snapshot-id> [--force]
```

### `diff`

Show what changed between two snapshots.

```
straylight-snapshot diff <snapshot-a> <snapshot-b>
```

### `send`

Send a snapshot to a remote host.

```
straylight-snapshot send <snapshot-id> --to <host>:<path> [--incremental]
```

### `browse`

Mount a snapshot read-only for file browsing.

```
straylight-snapshot browse <snapshot-id> [--mountpoint <path>]
```

### `policy`

Manage retention policies.

```
straylight-snapshot policy show [<filesystem>]
straylight-snapshot policy set <filesystem> --hourly <n> --daily <n> --weekly <n>
```

### `prune`

Run the retention policy and remove expired snapshots.

```
straylight-snapshot prune [<filesystem>] [--dry-run]
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--json` | JSON output |
| `--readonly` | Create read-only snapshot (default) |
| `--verbose` | Debug logging |

## CONFIGURATION

```toml
[snapshot]
backend = "btrfs"           # btrfs | zfs
store_path = "/snapshots"
auto_prune = true

[retention]
hourly = 24
daily = 30
weekly = 26
monthly = 12

[send]
compress = true
encrypt = true
bandwidth_limit = "100M"
```

## EXAMPLES

Create a tagged snapshot:

```
straylight-snapshot create / --tag "before-upgrade"
```

List all snapshots:

```
straylight-snapshot list --sort date
```

See what changed between two snapshots:

```
straylight-snapshot diff snap-20260314 snap-20260315
```

Restore a specific file from a snapshot:

```
straylight-snapshot browse snap-20260314 --mountpoint /mnt/snap
cp /mnt/snap/etc/nginx/nginx.conf /etc/nginx/nginx.conf
```

Send incremental backup to a remote host:

```
straylight-snapshot send snap-20260315 --to backup-host:/backups --incremental
```

Set retention policy:

```
straylight-snapshot policy set / --hourly 48 --daily 60 --weekly 52
```

## INTEGRATION

- **straylight-echo**: Snapshots complement echo's change-level undo.
- **straylight-mirror**: Snapshots can be taken from mirrors to avoid source I/O.
- **straylight-migrate**: Migration takes a snapshot before starting for safety.
- **straylight-cron**: Snapshot creation is commonly scheduled.
- **straylight-update**: System updates trigger pre-update snapshots.
- **straylight-boot**: Boot environments are backed by filesystem snapshots.

## SEE ALSO

straylight-echo(1), straylight-mirror(1), straylight-migrate(1), straylight-boot(1), straylight-disk(1)
