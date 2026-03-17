# straylight-disk

## NAME

straylight-disk -- disk management covering partitioning, RAID, encryption, and SMART monitoring

## SYNOPSIS

```
straylight-disk [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-disk is the comprehensive storage management tool for StrayLight OS. It handles disk discovery, partitioning, filesystem creation, RAID assembly, LUKS encryption, mount management, and SMART health monitoring. The tool provides a unified interface for managing NVMe, SATA, and USB storage devices.

disk goes beyond basic storage management by providing predictive failure detection. It continuously monitors SMART attributes and uses regression analysis to predict when a drive is likely to fail, giving users time to migrate data before loss occurs. This prediction data feeds into straylight-alice for system-wide health assessment.

The tool supports live operations wherever possible: growing filesystems online, adding disks to RAID arrays without downtime, and re-encrypting volumes in place. All destructive operations require explicit confirmation and create straylight-echo checkpoints.

## COMMANDS

### `list`

List all storage devices and their status.

```
straylight-disk list [--json] [--verbose]
```

### `info`

Show detailed information about a device.

```
straylight-disk info <device> [--smart]
```

### `partition`

Manage partitions.

```
straylight-disk partition <device> create --size <size> --type <type>
straylight-disk partition <device> delete <number>
straylight-disk partition <device> resize <number> --size <size>
straylight-disk partition <device> list
```

### `format`

Create a filesystem.

```
straylight-disk format <partition> --fs <btrfs|ext4|xfs|zfs> [--label <name>]
```

### `mount` / `unmount`

Mount or unmount a filesystem.

```
straylight-disk mount <partition> <mountpoint> [--options <opts>]
straylight-disk unmount <mountpoint>
```

### `raid`

Manage RAID arrays.

```
straylight-disk raid create --level <0|1|5|6|10> --devices <list> [--name <name>]
straylight-disk raid status [<name>]
straylight-disk raid add <name> <device>
straylight-disk raid remove <name> <device>
```

### `encrypt`

Manage LUKS encryption.

```
straylight-disk encrypt <partition> [--cipher <cipher>]
straylight-disk encrypt open <partition> <name>
straylight-disk encrypt close <name>
```

### `smart`

SMART health monitoring.

```
straylight-disk smart <device> [--full]
straylight-disk smart predict <device>
```

### `benchmark`

Benchmark disk performance.

```
straylight-disk benchmark <device> [--type <sequential|random|mixed>]
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--json` | JSON output |
| `--force` | Skip confirmation for destructive operations |
| `--verbose` | Debug logging |

## CONFIGURATION

```toml
[disk]
smart_poll_interval = "1h"
smart_predict = true

[mount]
auto_mount = true
default_options = "noatime"

[encryption]
default_cipher = "aes-xts-plain64"
key_size = 512

[alerts]
smart_warning = true
space_threshold = 90        # percent
```

## EXAMPLES

List all disks and partitions:

```
straylight-disk list --verbose
```

Check SMART health with failure prediction:

```
straylight-disk smart /dev/nvme0 --full
straylight-disk smart predict /dev/nvme0
```

Create a RAID-1 array:

```
straylight-disk raid create --level 1 --devices /dev/sda1,/dev/sdb1 --name data-mirror
```

Encrypt and mount a partition:

```
straylight-disk encrypt /dev/sda2
straylight-disk encrypt open /dev/sda2 secure-data
straylight-disk format /dev/mapper/secure-data --fs btrfs --label secure
straylight-disk mount /dev/mapper/secure-data /mnt/secure
```

Benchmark a disk:

```
straylight-disk benchmark /dev/nvme0 --type sequential
```

## INTEGRATION

- **straylight-snapshot**: Filesystem snapshots are managed in coordination with disk.
- **straylight-mirror**: Live replication works at the disk level.
- **straylight-alice**: SMART predictions feed the AI health model.
- **straylight-health**: Disk health is a major component of system health score.
- **straylight-vault**: Encryption keys can be stored in vault.
- **straylight-fabric**: Disk devices appear in the device topology graph.
- **straylight-echo**: Partition and format operations are registered for audit.

## SEE ALSO

straylight-snapshot(1), straylight-mirror(1), straylight-health(1), straylight-vault(1), straylight-fabric(1)
