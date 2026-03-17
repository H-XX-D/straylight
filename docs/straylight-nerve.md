# straylight-nerve

## NAME

straylight-nerve -- IRQ affinity optimizer that pins interrupts to optimal CPU cores

## SYNOPSIS

```
straylight-nerve [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-nerve optimizes interrupt request (IRQ) affinity by pinning hardware interrupts to the CPU cores best positioned to handle them. Using topology data from straylight-fabric, nerve ensures that NVMe interrupts land on cores in the same NUMA node as the storage controller, that network card interrupts are spread across cores connected to the NIC's PCIe root complex, and that GPU interrupts avoid cores reserved for latency-sensitive workloads.

Modern systems can have hundreds of IRQ lines, and the kernel's default irqbalance does not consider PCIe topology, NUMA distance, or workload characteristics. nerve replaces irqbalance with a topology-aware optimizer that reduces cross-NUMA traffic and cache contention, delivering measurable latency improvements for I/O-heavy workloads.

nerve operates as a daemon that continuously monitors interrupt rates and rebalances as needed. It can also be run in one-shot mode for static configurations.

## COMMANDS

### `optimize`

Calculate and apply optimal IRQ affinity.

```
straylight-nerve optimize [--strategy <numa|spread|pack>] [--dry-run]
```

### `status`

Show current IRQ affinity map.

```
straylight-nerve status [--json] [--verbose]
```

### `pin`

Manually pin an IRQ to specific cores.

```
straylight-nerve pin <irq> --cores <list>
```

### `unpin`

Remove a manual pin.

```
straylight-nerve unpin <irq>
```

### `daemon`

Run as a continuous optimization daemon.

```
straylight-nerve daemon [--interval <duration>]
```

### `report`

Generate an IRQ distribution report.

```
straylight-nerve report [--format <text|json|html>]
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--strategy <name>` | Optimization strategy |
| `--topology <numa\|flat>` | Topology mode |
| `--dry-run` | Show changes without applying |
| `--json` | JSON output |
| `--verbose` | Debug logging |

## CONFIGURATION

```toml
[nerve]
strategy = "numa"
rebalance_interval = "60s"
replace_irqbalance = true

[reserved_cores]
# Cores to exclude from IRQ handling
exclude = [0, 1]

[priorities]
nvme = "high"
network = "high"
usb = "low"
gpu = "medium"
```

## EXAMPLES

Apply NUMA-aware optimization:

```
straylight-nerve optimize --strategy numa
```

Preview changes without applying:

```
straylight-nerve optimize --dry-run
```

Pin NVMe IRQs to cores 4-7:

```
straylight-nerve pin 42 --cores 4,5,6,7
```

Run as a daemon:

```
straylight-nerve daemon --interval 30s
```

Generate an HTML report:

```
straylight-nerve report --format html > irq-report.html
```

Show current IRQ mapping:

```
straylight-nerve status --verbose
```

## INTEGRATION

- **straylight-fabric**: Topology data drives IRQ-to-core mapping decisions.
- **straylight-autotune**: Coordinates with autotune to avoid conflicting CPU policies.
- **straylight-thermal**: Avoids pinning IRQs to thermally throttled cores.
- **straylight-capsule**: Respects core reservations from resource contracts.
- **straylight-bench**: Benchmark results can validate nerve optimizations.

## SEE ALSO

straylight-fabric(1), straylight-autotune(1), straylight-thermal(1), straylight-capsule(1), straylight-bench(1)
