# straylight-mirror

## NAME

straylight-mirror -- live system cloning to a secondary disk or remote host

## SYNOPSIS

```
straylight-mirror [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-mirror creates and maintains an exact replica of a StrayLight system on a secondary disk, partition, or remote host. Unlike snapshots (which capture a point-in-time state), mirror maintains a continuously synchronized copy that tracks the source in near-real-time. This provides instant failover capability and simplifies disaster recovery.

mirror operates at the block level for raw partitions and at the filesystem level for Btrfs/ZFS datasets. Block-level mirroring uses dm-mirror or DRBD for local and remote targets respectively. Filesystem-level mirroring uses incremental send/receive for bandwidth efficiency.

The tool supports both synchronous mirroring (every write is committed to both copies before returning) and asynchronous mirroring (writes are buffered and replicated with a configurable lag). Synchronous mode guarantees zero data loss but adds write latency; asynchronous mode is suitable for remote mirrors over higher-latency links.

## COMMANDS

### `create`

Set up a new mirror relationship.

```
straylight-mirror create <source> --target <destination> [--mode <sync|async>]
```

### `status`

Show mirror synchronization status.

```
straylight-mirror status [<mirror-name>] [--json]
```

### `pause`

Pause mirroring (source continues operating).

```
straylight-mirror pause <mirror-name>
```

### `resume`

Resume a paused mirror.

```
straylight-mirror resume <mirror-name>
```

### `promote`

Promote a mirror target to primary (failover).

```
straylight-mirror promote <mirror-name>
```

### `destroy`

Remove a mirror relationship.

```
straylight-mirror destroy <mirror-name> [--keep-target]
```

### `verify`

Check data consistency between source and target.

```
straylight-mirror verify <mirror-name> [--deep]
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--mode <sync\|async>` | Mirroring mode |
| `--bandwidth <limit>` | Bandwidth limit for replication |
| `--json` | JSON output |
| `--verbose` | Debug logging |

## CONFIGURATION

```toml
[mirror]
default_mode = "async"
verify_interval = "24h"

[async]
max_lag = "30s"
buffer_size = "1G"

[transport]
compress = true
encrypt = true

[failover]
auto_promote = false
health_check_interval = "5s"
```

## EXAMPLES

Mirror the root filesystem to a second disk:

```
straylight-mirror create / --target /dev/sdb1 --mode sync
```

Set up an async remote mirror:

```
straylight-mirror create / --target remote-host:/backup --mode async
```

Check mirror status:

```
straylight-mirror status
```

Failover to the mirror:

```
straylight-mirror promote root-mirror
```

Deep verification of data consistency:

```
straylight-mirror verify root-mirror --deep
```

## INTEGRATION

- **straylight-migrate**: Migration uses mirror for bulk data transfer.
- **straylight-snapshot**: Snapshots can be taken from the mirror to avoid source I/O impact.
- **straylight-disk**: Disk management coordinates with mirror for partition changes.
- **straylight-health**: Mirror lag and consistency contribute to the health score.
- **straylight-boot**: Boot configuration is kept synchronized on the mirror target.

## SEE ALSO

straylight-snapshot(1), straylight-migrate(1), straylight-disk(1), straylight-boot(1), straylight-health(1)
