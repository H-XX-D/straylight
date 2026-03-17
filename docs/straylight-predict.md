# straylight-predict

## NAME

straylight-predict -- predictive preloading engine that anticipates resource needs before they arise

## SYNOPSIS

```
straylight-predict [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-predict learns user behavior patterns and proactively prepares the system for what is likely to happen next. It observes application launch sequences, file access patterns, network connections, and time-of-day routines, then uses this data to preload binaries into the page cache, warm DNS caches, pre-fork application processes, and pre-allocate memory.

The result is a system that feels instantaneously responsive. When a user sits down at 9 AM, their IDE, browser, and communication tools are already warm in memory. When a cron job is about to run a heavy compilation, the compiler and source files are already cached.

predict uses a lightweight Markov model augmented with time-series features from straylight-alice. It runs with minimal CPU overhead (typically under 0.5%) and can be constrained to use only a specified amount of memory for preloading. All predictions are probabilistic -- predict never preloads more data than it is confident will be used.

## COMMANDS

### `start`

Start the prediction daemon.

```
straylight-predict start [--memory-budget <size>]
```

### `stop`

Stop the prediction daemon.

```
straylight-predict stop
```

### `status`

Show prediction accuracy and cache state.

```
straylight-predict status [--json]
```

### `learn`

Manually trigger a learning cycle.

```
straylight-predict learn [--window <duration>]
```

### `predict`

Show what predict would preload right now.

```
straylight-predict predict [--verbose]
```

### `exclude`

Exclude paths or applications from preloading.

```
straylight-predict exclude <path|app>
```

### `stats`

Show hit rate and performance metrics.

```
straylight-predict stats [--since <datetime>]
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--memory-budget <size>` | Maximum memory for preloading (default: 10% of RAM) |
| `--confidence <threshold>` | Minimum confidence to preload (0.0-1.0, default: 0.7) |
| `--json` | JSON output |
| `--verbose` | Debug logging |

## CONFIGURATION

```toml
[predict]
memory_budget = "10%"
confidence_threshold = 0.7
learning_window = "14d"
model_path = "/var/lib/straylight/predict/model.bin"

[sources]
applications = true
files = true
dns = true
network = false

[schedule]
# Time-aware patterns
work_hours = "08:00-18:00"
enabled_on_battery = false

[exclusions]
paths = ["/tmp", "/var/cache"]
apps = []
```

## EXAMPLES

Start with a 4 GiB memory budget:

```
straylight-predict start --memory-budget 4G
```

Check prediction accuracy:

```
straylight-predict stats --since "7d ago"
```

See what would be preloaded now:

```
straylight-predict predict --verbose
```

Exclude large build directories:

```
straylight-predict exclude /home/user/builds
```

Show current cache state:

```
straylight-predict status --json
```

## INTEGRATION

- **straylight-alice**: AI forecasts supplement predict's behavioral model.
- **straylight-power**: Prediction is paused on battery to save power.
- **straylight-capsule**: Preloading respects capsule memory guarantees.
- **straylight-cron**: Scheduled task patterns inform preloading.
- **straylight-timeline**: Prediction accuracy is logged to the timeline.

## SEE ALSO

straylight-alice(1), straylight-power(1), straylight-capsule(1), straylight-cron(1), straylight-autotune(1)
