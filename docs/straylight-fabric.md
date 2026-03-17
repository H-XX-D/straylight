# straylight-fabric

## NAME

straylight-fabric -- device topology graph builder for PCIe, USB, and NVLink trees

## SYNOPSIS

```
straylight-fabric [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-fabric discovers and maps the physical and logical topology of all hardware devices in the system. It builds a graph of PCIe lanes, USB hubs, NVLink bridges, NUMA nodes, and interconnect bandwidths, then exposes this information to other StrayLight tools that need to make placement decisions.

Understanding device topology is critical for performance. A GPU that shares a PCIe root complex with an NVMe drive can transfer data faster than one separated by an inter-socket link. straylight-fabric makes this knowledge accessible so that straylight-mesh can place GPU workloads optimally, straylight-nerve can pin interrupts to the right cores, and straylight-autotune can configure NUMA policies correctly.

fabric also monitors for hot-plug events and topology changes, updating the graph in real time and notifying subscribers over the StrayLight bus.

## COMMANDS

### `map`

Build and display the device topology graph.

```
straylight-fabric map [--format <tree|dot|json|svg>] [--filter <type>]
```

### `query`

Query relationships between devices.

```
straylight-fabric query <device-a> <device-b> [--metric <bandwidth|latency|hops>]
```

### `list`

List all discovered devices.

```
straylight-fabric list [--type <pcie|usb|nvlink|numa>] [--json]
```

### `watch`

Monitor for topology changes.

```
straylight-fabric watch [--events <add|remove|change>]
```

### `benchmark`

Measure actual interconnect bandwidth between devices.

```
straylight-fabric benchmark <device-a> <device-b> [--duration <seconds>]
```

### `export`

Export the topology graph.

```
straylight-fabric export --format <dot|json|yaml> --output <path>
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--format <fmt>` | Output format (tree, dot, json, svg) |
| `--filter <type>` | Filter by device type |
| `--json` | JSON output |
| `--verbose` | Debug logging |

## CONFIGURATION

```toml
[fabric]
scan_interval = "60s"
include_virtual = false
cache_path = "/var/lib/straylight/fabric"

[display]
default_format = "tree"
show_bandwidth = true
show_numa = true

[hotplug]
notify = true
auto_rescan = true
```

## EXAMPLES

Display the full device tree:

```
straylight-fabric map
```

Generate a Graphviz diagram:

```
straylight-fabric map --format dot | dot -Tpng -o topology.png
```

Check the bandwidth between GPU 0 and NVMe 0:

```
straylight-fabric query gpu:0 nvme:0 --metric bandwidth
```

List all PCIe devices:

```
straylight-fabric list --type pcie --json
```

Monitor for USB hot-plug events:

```
straylight-fabric watch --events add,remove --filter usb
```

Benchmark the link between two GPUs:

```
straylight-fabric benchmark gpu:0 gpu:1 --duration 10
```

## INTEGRATION

- **straylight-nerve**: Uses topology data to calculate optimal IRQ-to-core mappings.
- **straylight-mesh**: Topology-aware GPU workload placement.
- **straylight-autotune**: NUMA topology informs memory and scheduler tuning.
- **straylight-alice**: Topology anomalies (missing devices, degraded links) trigger alerts.
- **straylight-display**: Display topology for multi-GPU rendering decisions.

## SEE ALSO

straylight-nerve(1), straylight-mesh(1), straylight-autotune(1), straylight-display(1), straylight-disk(1)
