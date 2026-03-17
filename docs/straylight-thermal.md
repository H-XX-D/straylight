# straylight-thermal

## NAME

straylight-thermal -- thermal zone management and fan-curve control

## SYNOPSIS

```
straylight-thermal [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-thermal manages the thermal behavior of the system. It reads temperatures from CPU, GPU, NVMe, and ambient sensors, controls fan speeds via custom fan curves, and implements thermal throttling policies to prevent hardware damage. The tool replaces the kernel's default thermal governor with a more intelligent controller that considers workload priority and user preferences.

Unlike simple threshold-based fan control, straylight-thermal uses a PID controller that smoothly adjusts fan speed based on the rate of temperature change, not just the absolute temperature. This prevents the annoying fan speed oscillation common in many systems. Users can define custom fan curves or use the built-in profiles: silent (prioritizes noise reduction), balanced, and performance (fans run proactively to keep components cool).

straylight-thermal also supports GPU thermal management via vendor-specific interfaces (NVIDIA, AMD) and NVMe temperature monitoring via SMART attributes.

## COMMANDS

### `status`

Show current temperatures and fan speeds.

```
straylight-thermal status [--json] [--watch]
```

### `set`

Set the thermal profile.

```
straylight-thermal set <silent|balanced|performance|custom>
```

### `curve`

Manage custom fan curves.

```
straylight-thermal curve show [--fan <name>]
straylight-thermal curve set --fan <name> --points "<temp>:<speed>,..."
```

### `limit`

Set temperature limits.

```
straylight-thermal limit <zone> --max <temp>
```

### `history`

Show temperature history.

```
straylight-thermal history [--zone <name>] [--since <datetime>] [--format <text|json|csv>]
```

### `test`

Run a thermal stress test.

```
straylight-thermal test [--duration <duration>] [--zone <name>]
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--json` | JSON output |
| `--watch` | Continuously update display |
| `--verbose` | Debug logging |

## CONFIGURATION

```toml
[thermal]
profile = "balanced"
poll_interval = "2s"
hysteresis = 3              # degrees C

[fans.cpu]
curve = [[30, 0], [50, 30], [70, 60], [85, 100]]
min_speed = 0
max_speed = 100

[fans.gpu]
curve = [[40, 0], [60, 40], [80, 70], [90, 100]]

[limits]
cpu_max = 95
gpu_max = 90
nvme_max = 70

[throttle]
cpu_throttle_temp = 90
gpu_throttle_temp = 85
```

## EXAMPLES

Check temperatures:

```
straylight-thermal status
```

Set silent profile:

```
straylight-thermal set silent
```

Watch temperatures continuously:

```
straylight-thermal status --watch
```

Set a custom CPU fan curve:

```
straylight-thermal curve set --fan cpu --points "30:0,50:25,65:50,80:80,90:100"
```

View temperature history:

```
straylight-thermal history --zone cpu --since "24h ago" --format csv
```

Run a thermal stress test:

```
straylight-thermal test --duration 5m
```

## INTEGRATION

- **straylight-autotune**: Thermal state influences CPU frequency and scheduler decisions.
- **straylight-alice**: Temperature anomalies trigger AI alerts.
- **straylight-health**: Thermal status is a component of the system health score.
- **straylight-dash / straylight-hub**: Temperature gauges on the dashboard.
- **straylight-bench**: Benchmarks wait for thermal equilibrium.
- **straylight-nerve**: IRQs avoid thermally throttled cores.

## SEE ALSO

straylight-autotune(1), straylight-alice(1), straylight-health(1), straylight-power(1), straylight-bench(1)
