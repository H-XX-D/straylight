# straylight-alice

## NAME

straylight-alice -- AI system monitor that detects anomalies, forecasts failures, and suggests remediations

## SYNOPSIS

```
straylight-alice [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-alice is the AI-powered monitoring backbone of StrayLight OS. It continuously ingests telemetry from every subsystem -- CPU, memory, disk, network, GPU, thermal sensors, and application metrics -- and feeds them through a lightweight on-device transformer model to detect anomalies in real time.

Unlike traditional threshold-based monitoring, alice learns the normal behavioral envelope of the specific machine it runs on. After an initial 24-hour calibration period it can distinguish between a legitimate workload spike and a genuine fault with high accuracy. When an anomaly is detected, alice classifies its severity, identifies the probable root cause, and emits a structured remediation suggestion that other tools (such as straylight-autotune or straylight-thermal) can act on automatically.

alice also provides a forecasting mode that predicts resource exhaustion, disk failure probability (via SMART regression), and thermal throttling events hours or days in advance. Forecasts are published to the StrayLight bus so that straylight-predict and straylight-cron can pre-emptively adjust workloads.

## COMMANDS

### `watch`

Start the real-time monitoring loop.

```
straylight-alice watch [--sensitivity <low|medium|high>] [--subsystems <list>]
```

Runs in the foreground, printing anomaly alerts as they occur. Use `--sensitivity` to tune the false-positive / false-negative trade-off. The `--subsystems` flag accepts a comma-separated list (e.g., `cpu,disk,network`) to limit scope.

### `status`

Show current system health as assessed by the AI model.

```
straylight-alice status [--json]
```

Prints a summary table of every monitored subsystem, its current state (normal, warning, critical), and the model's confidence score.

### `forecast`

Generate a forward-looking prediction report.

```
straylight-alice forecast [--horizon <duration>] [--format <text|json|yaml>]
```

The `--horizon` flag accepts durations like `6h`, `1d`, or `7d`. Default is `24h`.

### `explain`

Provide a natural-language explanation for a specific alert.

```
straylight-alice explain <alert-id>
```

Returns the reasoning chain the model followed to classify the anomaly, including the contributing signals and their weights.

### `train`

Trigger a manual re-calibration of the baseline model.

```
straylight-alice train [--window <duration>] [--include-current]
```

Useful after a major hardware change. The `--window` flag sets how much historical data to use. `--include-current` incorporates the current workload into the new baseline.

### `suppress`

Suppress a known-benign alert pattern.

```
straylight-alice suppress <pattern> [--duration <duration>] [--reason <text>]
```

Prevents matched alerts from firing for the specified duration. Suppressions are logged for audit.

### `history`

Query past alerts and predictions.

```
straylight-alice history [--since <datetime>] [--severity <level>] [--subsystem <name>]
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Path to configuration file (default: `/etc/straylight/alice.toml`) |
| `--model <path>` | Path to the ONNX model file |
| `--bus <socket>` | StrayLight bus socket path |
| `--json` | Output in JSON format |
| `--quiet` | Suppress non-critical output |
| `--verbose` | Enable debug-level logging |
| `--no-color` | Disable colored output |

## CONFIGURATION

Configuration is read from `/etc/straylight/alice.toml` with user overrides in `~/.config/straylight/alice.toml`.

```toml
[model]
path = "/usr/share/straylight/models/alice-v3.onnx"
calibration_window = "7d"
sensitivity = "medium"

[subsystems]
enabled = ["cpu", "memory", "disk", "network", "gpu", "thermal"]

[alerts]
cooldown = "5m"
max_per_hour = 30
delivery = ["bus", "notify"]

[forecast]
default_horizon = "24h"
update_interval = "1h"

[training]
auto_retrain = true
retrain_interval = "7d"
min_data_points = 10000
```

## EXAMPLES

Watch all subsystems with high sensitivity:

```
straylight-alice watch --sensitivity high
```

Get a JSON status report:

```
straylight-alice status --json
```

Forecast disk and memory for the next week:

```
straylight-alice forecast --horizon 7d --subsystems disk,memory
```

Explain why alert #4821 was raised:

```
straylight-alice explain 4821
```

Suppress thermal warnings during a known-heavy render job:

```
straylight-alice suppress "thermal.*warning" --duration 2h --reason "Blender render in progress"
```

Retrain the model after installing new RAM:

```
straylight-alice train --window 3d --include-current
```

Show all critical alerts from the past 48 hours:

```
straylight-alice history --since "48h ago" --severity critical
```

## INTEGRATION

- **straylight-autotune**: alice publishes tuning recommendations that autotune can apply automatically.
- **straylight-thermal**: Thermal anomalies detected by alice trigger thermal policy adjustments.
- **straylight-predict**: alice forecasts feed directly into the predictive preloading engine.
- **straylight-notify**: Alerts are delivered through the notification daemon with appropriate priority.
- **straylight-health**: alice is the primary data source for the health monitor's AI-driven assessments.
- **straylight-cron**: Scheduled tasks can be gated on alice's health assessment.
- **straylight-hub / straylight-dash**: Both dashboards display alice's status and alert stream.

## SEE ALSO

straylight-health(1), straylight-autotune(1), straylight-predict(1), straylight-thermal(1), straylight-notify(1)
