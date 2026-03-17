# straylight-autotune

## NAME

straylight-autotune -- kernel tuning daemon that continuously optimizes sysctl, scheduler, and memory parameters

## SYNOPSIS

```
straylight-autotune [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-autotune is a persistent daemon that monitors system workload characteristics and dynamically adjusts kernel parameters to maintain optimal performance. It manages sysctl values, CPU scheduler policies, memory reclaim thresholds, I/O elevator settings, and network buffer sizes without requiring manual intervention or reboots.

The daemon operates in a closed feedback loop: it applies a parameter change, measures the effect over a configurable observation window, and either commits the change or rolls it back. This approach prevents misconfigurations from degrading system performance. All applied tunings are recorded in a versioned ledger so that any change can be inspected or reverted.

straylight-autotune ships with workload profiles (desktop, server, realtime, ml-training, database) that provide sensible starting points. The daemon refines these profiles over time using telemetry from straylight-alice. Administrators can also define custom profiles and pin specific parameters to prevent automatic adjustment.

## COMMANDS

### `start`

Start the tuning daemon.

```
straylight-autotune start [--profile <name>] [--dry-run]
```

Launches the daemon in the background. With `--dry-run`, changes are computed and logged but never applied.

### `stop`

Stop the daemon and optionally revert all changes.

```
straylight-autotune stop [--revert]
```

### `status`

Show the current tuning state.

```
straylight-autotune status [--json] [--verbose]
```

Displays active profile, number of parameters modified, and the daemon's assessment of current system fitness.

### `apply`

Apply a specific profile immediately.

```
straylight-autotune apply <profile> [--force]
```

### `diff`

Show what the daemon would change relative to kernel defaults.

```
straylight-autotune diff [--profile <name>]
```

### `pin`

Lock a parameter so autotune will not modify it.

```
straylight-autotune pin <parameter> [--value <val>]
```

### `unpin`

Remove a parameter lock.

```
straylight-autotune unpin <parameter>
```

### `history`

Show the tuning changelog.

```
straylight-autotune history [--since <datetime>] [--param <name>]
```

### `rollback`

Revert to a previous tuning state.

```
straylight-autotune rollback <version|timestamp>
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--profile <name>` | Workload profile to use |
| `--dry-run` | Compute but do not apply changes |
| `--interval <duration>` | Observation window between adjustments (default: `30s`) |
| `--json` | JSON output |
| `--verbose` | Debug logging |

## CONFIGURATION

```toml
[daemon]
profile = "desktop"
interval = "30s"
observation_window = "5m"
max_rollbacks = 100

[scheduler]
auto_adjust = true
allowed_policies = ["SCHED_NORMAL", "SCHED_BATCH"]

[memory]
auto_adjust = true
swappiness_range = [10, 60]
thp = "madvise"

[network]
auto_adjust = true
tcp_buffer_auto = true

[pinned]
# Parameters that autotune must not modify
"vm.overcommit_memory" = 0

[safety]
revert_on_regression = true
regression_threshold = 0.05
```

## EXAMPLES

Start with the ML training profile:

```
straylight-autotune start --profile ml-training
```

See what would change without applying:

```
straylight-autotune diff --profile server
```

Pin swappiness to a fixed value:

```
straylight-autotune pin vm.swappiness --value 10
```

Rollback to the state from 2 hours ago:

```
straylight-autotune rollback "2h ago"
```

Show parameter change history for network buffers:

```
straylight-autotune history --param "net.core.rmem_max"
```

## INTEGRATION

- **straylight-alice**: Receives workload classification and anomaly signals to inform tuning decisions.
- **straylight-health**: Reports tuning effectiveness metrics.
- **straylight-thermal**: Coordinates CPU frequency and scheduler changes with thermal policy.
- **straylight-capsule**: Respects resource contracts when adjusting cgroup parameters.
- **straylight-echo**: Tuning changes are registered with echo for system-wide undo.

## SEE ALSO

straylight-alice(1), straylight-health(1), straylight-thermal(1), straylight-capsule(1), straylight-echo(1)
