# straylight-mesh

## NAME

straylight-mesh -- distributed GPU pool for transparent multi-node compute

## SYNOPSIS

```
straylight-mesh [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-mesh aggregates GPUs across multiple machines into a single virtual compute pool. Applications see a unified set of GPU devices regardless of their physical location, and mesh handles data placement, kernel dispatch, and memory transfer transparently. This enables workloads that exceed the GPU capacity of a single machine -- large model training, multi-GPU rendering, and distributed inference.

mesh builds a topology-aware schedule using data from straylight-fabric to minimize data movement. When two GPUs are connected via NVLink, mesh prefers to co-locate tensors that interact frequently. When GPUs span machines, mesh uses straylight-bridge for RDMA-backed transfers with microsecond-level latency.

The tool supports CUDA, ROCm, and Vulkan compute backends. It presents virtual devices through a compatibility layer so that existing applications work without modification. Advanced users can provide placement hints to override the automatic scheduler.

## COMMANDS

### `pool`

Manage the GPU pool.

```
straylight-mesh pool add <node> [--gpus <indices>]
straylight-mesh pool remove <node>
straylight-mesh pool list [--json]
straylight-mesh pool status
```

### `run`

Run a command using the mesh GPU pool.

```
straylight-mesh run [--gpus <n>] [--placement <strategy>] -- <command>
```

### `allocate`

Reserve GPUs for exclusive use.

```
straylight-mesh allocate <n> [--duration <duration>] [--node <preference>]
```

### `release`

Release a GPU reservation.

```
straylight-mesh release <reservation-id>
```

### `topology`

Display the GPU interconnect topology.

```
straylight-mesh topology [--format <tree|dot|json>]
```

### `benchmark`

Measure inter-GPU bandwidth and latency.

```
straylight-mesh benchmark [--all-pairs]
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--gpus <n>` | Number of GPUs to use |
| `--placement <strategy>` | Placement strategy: `pack`, `spread`, `topology-aware` |
| `--json` | JSON output |
| `--verbose` | Debug logging |

## CONFIGURATION

```toml
[mesh]
discovery = "multicast"     # multicast | static | consul
port = 9490
default_placement = "topology-aware"

[backends]
cuda = true
rocm = true
vulkan = false

[transport]
protocol = "rdma"           # rdma | tcp
bandwidth_limit = "100G"

[scheduling]
preemption = false
max_reservation_hours = 24
```

## EXAMPLES

Add a node's GPUs to the pool:

```
straylight-mesh pool add node-02 --gpus 0,1,2,3
```

Run a training job across 8 GPUs:

```
straylight-mesh run --gpus 8 --placement topology-aware -- python train.py
```

Check pool status:

```
straylight-mesh pool status
```

Reserve 4 GPUs for 2 hours:

```
straylight-mesh allocate 4 --duration 2h
```

View GPU interconnect topology:

```
straylight-mesh topology --format dot | dot -Tpng -o gpu-topo.png
```

Benchmark all GPU pairs:

```
straylight-mesh benchmark --all-pairs
```

## INTEGRATION

- **straylight-fabric**: Provides PCIe/NVLink topology data for placement decisions.
- **straylight-bridge**: RDMA transport layer for cross-machine GPU memory transfers.
- **straylight-swarm**: Cluster orchestration uses mesh for GPU-aware scheduling.
- **straylight-ghost**: GPU context migration between nodes.
- **straylight-capsule**: GPU resources are part of capsule resource contracts.
- **straylight-alice**: Monitors GPU health and utilization.

## SEE ALSO

straylight-fabric(1), straylight-bridge(1), straylight-swarm(1), straylight-ghost(1), straylight-capsule(1)
