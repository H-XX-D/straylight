# straylight-ghost

## NAME

straylight-ghost -- process migration that relocates running processes between nodes

## SYNOPSIS

```
straylight-ghost [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-ghost enables live migration of running processes between machines in a StrayLight cluster. A process can be frozen on one node, its entire state (memory, file descriptors, network connections, GPU context) serialized, transferred to another node, and resumed -- all without the process being aware that it moved.

ghost builds on CRIU (Checkpoint/Restore In Userspace) with StrayLight-specific extensions for GPU state, eBPF maps, and io_uring contexts. It coordinates with straylight-bridge to pre-establish shared memory regions on the target node, and with straylight-mesh to ensure GPU resources are available at the destination.

The tool supports both manual migration (an operator decides to move a process) and policy-driven migration (straylight-swarm decides to rebalance the cluster). Migration can be performed live with minimal downtime (typically under 100ms for processes with less than 1 GiB of resident memory).

## COMMANDS

### `migrate`

Migrate a process to another node.

```
straylight-ghost migrate <pid> --to <node> [--live] [--timeout <duration>]
```

### `prepare`

Pre-stage a migration (warm the destination).

```
straylight-ghost prepare <pid> --to <node>
```

### `status`

Check migration status.

```
straylight-ghost status [<migration-id>]
```

### `abort`

Cancel an in-progress migration.

```
straylight-ghost abort <migration-id>
```

### `list`

Show migration history.

```
straylight-ghost list [--active] [--json]
```

### `estimate`

Estimate migration time and downtime for a process.

```
straylight-ghost estimate <pid> --to <node>
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--live` | Use iterative memory pre-copy for minimal downtime |
| `--compress` | Compress state during transfer |
| `--encrypt` | Encrypt state in transit |
| `--json` | JSON output |
| `--verbose` | Debug logging |

## CONFIGURATION

```toml
[ghost]
default_mode = "live"
compress = true
encrypt = true
max_downtime_ms = 100

[transport]
protocol = "rdma"          # rdma | tcp
port = 9470
bandwidth_limit = "10G"

[gpu]
migrate_context = true
supported_backends = ["cuda", "vulkan"]
```

## EXAMPLES

Live-migrate a process to another node:

```
straylight-ghost migrate 4821 --to node-02 --live
```

Estimate how long a migration will take:

```
straylight-ghost estimate 4821 --to node-02
```

Pre-stage a large-memory process:

```
straylight-ghost prepare 4821 --to node-02
straylight-ghost migrate 4821 --to node-02 --live
```

Migrate with encryption:

```
straylight-ghost migrate 4821 --to node-02 --live --encrypt
```

View active migrations:

```
straylight-ghost list --active --json
```

## INTEGRATION

- **straylight-swarm**: Swarm uses ghost to rebalance workloads across cluster nodes.
- **straylight-bridge**: Pre-establishes shared memory on the target for seamless migration.
- **straylight-mesh**: Verifies GPU availability at the destination before migration.
- **straylight-rewind**: Checkpoints created during migration can be used as restore points.
- **straylight-whisper**: Encrypted IPC channels are re-established transparently after migration.

## SEE ALSO

straylight-swarm(1), straylight-rewind(1), straylight-bridge(1), straylight-mesh(1), straylight-migrate(1)
