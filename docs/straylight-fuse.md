# straylight-fuse

## NAME

straylight-fuse -- process fusion engine that merges cooperating processes into a shared address space

## SYNOPSIS

```
straylight-fuse [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-fuse eliminates inter-process communication overhead by merging two or more cooperating processes into a shared address space. When processes communicate heavily via pipes, sockets, or shared memory, the context-switch and data-copy overhead can be significant. Fuse analyzes communication patterns and, when safe, relocates the hot paths into a single address space with shared memory regions.

The fusion is transparent to the applications -- they continue to use their original IPC APIs, but the underlying transport is replaced with direct memory access. Fuse uses eBPF probes to monitor IPC traffic and identify fusion candidates automatically. Administrators can also manually specify which processes to fuse.

Safety is enforced through hardware memory protection keys (Intel PKU / ARM MTE). Each fused process retains its own protection domain within the shared address space, preventing unintended cross-access while still enabling zero-copy data transfer on the designated channels.

## COMMANDS

### `analyze`

Identify fusion candidates based on IPC traffic.

```
straylight-fuse analyze [--threshold <msgs/sec>] [--duration <seconds>]
```

### `merge`

Fuse two or more processes.

```
straylight-fuse merge <pid1> <pid2> [<pid3>...] [--channel <type>]
```

### `split`

Undo a fusion, restoring independent processes.

```
straylight-fuse split <fusion-id>
```

### `list`

List active fusions.

```
straylight-fuse list [--json]
```

### `stats`

Show performance metrics for a fusion.

```
straylight-fuse stats <fusion-id>
```

### `monitor`

Continuously watch IPC traffic for auto-fusion opportunities.

```
straylight-fuse monitor [--auto-merge] [--threshold <msgs/sec>]
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--json` | JSON output |
| `--dry-run` | Analyze without merging |
| `--verbose` | Debug logging |
| `--safety <strict\|relaxed>` | Memory protection level |

## CONFIGURATION

```toml
[fuse]
auto_merge = false
ipc_threshold = 10000       # messages/sec to trigger analysis
safety = "strict"

[protection]
use_pku = true
use_mte = true

[limits]
max_fused_processes = 8
max_shared_memory = "4G"
```

## EXAMPLES

Find processes that would benefit from fusion:

```
straylight-fuse analyze --threshold 5000 --duration 30
```

Fuse a web server and its application backend:

```
straylight-fuse merge 1234 1235 --channel pipe
```

Run in auto-fusion monitoring mode:

```
straylight-fuse monitor --auto-merge --threshold 10000
```

Check fusion performance gains:

```
straylight-fuse stats fusion-001
```

Split a fusion:

```
straylight-fuse split fusion-001
```

## INTEGRATION

- **straylight-splice**: Fuse handles process-level merging; splice handles zero-copy I/O pipelines.
- **straylight-trace**: Tracing works transparently across fused processes.
- **straylight-capsule**: Fused process groups share a single capsule resource contract.
- **straylight-sandbox**: Fusion respects sandbox boundaries and will not merge across isolation domains.
- **straylight-alice**: AI monitor detects when fusion would improve performance.

## SEE ALSO

straylight-splice(1), straylight-capsule(1), straylight-sandbox(1), straylight-trace(1), straylight-whisper(1)
