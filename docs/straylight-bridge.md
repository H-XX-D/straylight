# straylight-bridge

## NAME

straylight-bridge -- cross-machine shared memory over RDMA or TCP fallback

## SYNOPSIS

```
straylight-bridge [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-bridge extends shared memory semantics across machine boundaries. Two processes on different nodes can map the same memory region and read/write it as though it were local, with bridge handling the transport transparently. On networks with RDMA support (InfiniBand, RoCE), this operates at microsecond latencies. On standard Ethernet, bridge uses a TCP-based coherence protocol with configurable consistency guarantees.

bridge is the foundation for several higher-level StrayLight tools. straylight-mesh uses it for GPU memory sharing across nodes. straylight-ghost uses it to pre-stage process memory during live migration. straylight-swarm uses it for distributed coordination.

The tool supports multiple consistency models: strict (every read sees the latest write), eventual (reads may be stale by a bounded duration), and owner-write (only one node writes at a time, reads are always consistent). The choice depends on the workload's tolerance for stale data versus its sensitivity to latency.

## COMMANDS

### `create`

Create a shared memory region.

```
straylight-bridge create <region-name> --size <bytes> --nodes <list> [--mode <strict|eventual|owner>]
```

### `attach`

Attach a process to a shared region.

```
straylight-bridge attach <region-name> --pid <pid>
```

### `detach`

Detach a process from a shared region.

```
straylight-bridge detach <region-name> --pid <pid>
```

### `list`

List active shared regions.

```
straylight-bridge list [--json]
```

### `status`

Show region statistics (latency, throughput, coherence state).

```
straylight-bridge status <region-name> [--json]
```

### `destroy`

Remove a shared region.

```
straylight-bridge destroy <region-name>
```

### `benchmark`

Measure cross-node memory performance.

```
straylight-bridge benchmark --node <target> [--size <bytes>] [--duration <seconds>]
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--mode <mode>` | Consistency mode |
| `--transport <rdma\|tcp>` | Transport protocol |
| `--json` | JSON output |
| `--verbose` | Debug logging |

## CONFIGURATION

```toml
[bridge]
port = 9488
transport = "auto"          # auto | rdma | tcp
default_mode = "eventual"

[rdma]
device = "mlx5_0"
gid_index = 0

[tcp]
buffer_size = "4M"
nagle = false

[coherence]
sync_interval = "1ms"
max_lag = "10ms"
```

## EXAMPLES

Create a 1 GiB shared region across two nodes:

```
straylight-bridge create model-weights --size 1G --nodes node-01,node-02 --mode strict
```

Attach a process:

```
straylight-bridge attach model-weights --pid 4821
```

Benchmark cross-node memory performance:

```
straylight-bridge benchmark --node node-02 --size 1G --duration 10
```

Check region latency:

```
straylight-bridge status model-weights --json
```

List all active regions:

```
straylight-bridge list
```

## INTEGRATION

- **straylight-mesh**: GPU memory sharing across nodes uses bridge as transport.
- **straylight-ghost**: Process migration pre-stages memory via bridge.
- **straylight-swarm**: Cluster coordination data structures use bridge.
- **straylight-fabric**: Network topology data informs bridge transport selection.
- **straylight-whisper**: Bridge channels can be encrypted via whisper.

## SEE ALSO

straylight-mesh(1), straylight-ghost(1), straylight-swarm(1), straylight-fabric(1), straylight-whisper(1)
