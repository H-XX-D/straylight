# straylight-rewind

## NAME

straylight-rewind -- process checkpointing and time-travel debugging

## SYNOPSIS

```
straylight-rewind [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-rewind creates snapshots of running processes that can be restored later. A checkpoint captures the complete process state: memory, registers, file descriptors, signal handlers, and pending I/O. The process can then be resumed from that checkpoint at any time, on the same machine or on a different one via straylight-ghost.

rewind's primary use case is debugging. Developers can checkpoint a process before a suspected bug, let it run, and if the bug occurs, rewind to the checkpoint and step through the code with a debugger. This "time-travel debugging" workflow eliminates the need to reproduce bugs -- the exact failing state is preserved.

rewind also supports periodic checkpointing for long-running computations. If a 12-hour simulation crashes at hour 10, it can be restarted from the last checkpoint instead of from the beginning.

## COMMANDS

### `checkpoint`

Create a checkpoint of a running process.

```
straylight-rewind checkpoint <pid> [--output <path>] [--tag <label>]
```

### `restore`

Restore a process from a checkpoint.

```
straylight-rewind restore <checkpoint> [--pid <pid>]
```

### `list`

List saved checkpoints.

```
straylight-rewind list [--pid <pid>] [--json]
```

### `auto`

Enable periodic checkpointing for a process.

```
straylight-rewind auto <pid> --interval <duration> [--keep <n>]
```

### `diff`

Compare two checkpoints.

```
straylight-rewind diff <checkpoint-a> <checkpoint-b>
```

### `delete`

Remove a checkpoint.

```
straylight-rewind delete <checkpoint> [--all]
```

### `inspect`

Examine a checkpoint without restoring.

```
straylight-rewind inspect <checkpoint> [--memory] [--fds] [--registers]
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--compress` | Compress checkpoint data |
| `--output <path>` | Checkpoint storage path |
| `--json` | JSON output |
| `--verbose` | Debug logging |

## CONFIGURATION

```toml
[rewind]
store_path = "/var/lib/straylight/rewind"
compress = true
max_store_size = "50G"

[auto]
default_interval = "5m"
default_keep = 5

[checkpoint]
include_file_contents = false
include_gpu_state = true
```

## EXAMPLES

Checkpoint a process:

```
straylight-rewind checkpoint 4821 --tag "before-migration"
```

Restore from a checkpoint:

```
straylight-rewind restore chk-4821-20260315
```

Set up periodic checkpointing every 10 minutes:

```
straylight-rewind auto 4821 --interval 10m --keep 6
```

Compare two checkpoints:

```
straylight-rewind diff chk-4821-a chk-4821-b
```

Inspect the file descriptors of a checkpoint:

```
straylight-rewind inspect chk-4821-20260315 --fds
```

## INTEGRATION

- **straylight-ghost**: Checkpoints are the mechanism for process migration.
- **straylight-replay**: Process-level checkpoints complement system-level flight recording.
- **straylight-trace**: Debugger integration uses rewind for time-travel debugging.
- **straylight-capsule**: Checkpoints respect capsule resource boundaries.
- **straylight-echo**: Checkpoint creation is logged for auditing.

## SEE ALSO

straylight-ghost(1), straylight-replay(1), straylight-trace(1), straylight-echo(1), straylight-snapshot(1)
