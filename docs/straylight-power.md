# straylight-power

## NAME

straylight-power -- power management, governor selection, and battery policy

## SYNOPSIS

```
straylight-power [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-power manages the power profile of StrayLight OS. It controls CPU frequency governors, GPU power states, display brightness, USB autosuspend, disk standby timers, and battery charging policies. The tool balances performance against power consumption and battery longevity based on the active power profile and current power source.

power ships with three profiles: performance (maximum speed, AC-optimized), balanced (adaptive to workload), and powersave (maximum battery life). Profiles are switched automatically based on power source (AC/battery) or can be set manually. The tool also supports custom profiles and workload-aware adjustments driven by straylight-alice.

For laptops, power provides battery health management features including charge thresholds (stop charging at 80% to extend battery lifespan), charge rate control, and battery wear estimation.

## COMMANDS

### `status`

Show current power state.

```
straylight-power status [--json]
```

### `profile`

Get or set the power profile.

```
straylight-power profile [performance|balanced|powersave]
```

### `battery`

Show battery information.

```
straylight-power battery [--json] [--health]
```

### `charge`

Configure battery charging.

```
straylight-power charge --threshold <percent>
straylight-power charge --rate <slow|normal|fast>
```

### `brightness`

Get or set display brightness.

```
straylight-power brightness [<percent>] [--display <name>]
```

### `sleep`

Configure sleep behavior.

```
straylight-power sleep [suspend|hibernate|hybrid] [--timeout <duration>]
```

### `governor`

Get or set CPU frequency governor.

```
straylight-power governor [performance|ondemand|powersave|schedutil]
```

### `wake`

Manage wake sources and scheduled wakes.

```
straylight-power wake list
straylight-power wake schedule <time> [--reason <text>]
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--json` | JSON output |
| `--verbose` | Debug logging |

## CONFIGURATION

```toml
[power]
default_profile = "balanced"
auto_switch = true          # Switch profile on AC/battery change

[profiles.balanced]
cpu_governor = "schedutil"
gpu_power = "adaptive"
brightness_ac = 80
brightness_battery = 50

[battery]
charge_threshold = 80
show_wear_estimate = true

[sleep]
idle_timeout_ac = "30m"
idle_timeout_battery = "10m"
lid_close = "suspend"
low_battery_action = "hibernate"
low_battery_threshold = 5

[usb]
autosuspend = true
autosuspend_delay = "2s"
```

## EXAMPLES

Check power status:

```
straylight-power status
```

Switch to performance profile:

```
straylight-power profile performance
```

Set battery charge threshold:

```
straylight-power charge --threshold 80
```

Set brightness to 60%:

```
straylight-power brightness 60
```

Check battery health:

```
straylight-power battery --health
```

Schedule a wake:

```
straylight-power wake schedule "06:00" --reason "morning backup"
```

## INTEGRATION

- **straylight-autotune**: CPU governor selection is coordinated with autotune.
- **straylight-thermal**: Power profiles interact with thermal management.
- **straylight-predict**: Prediction is paused on battery to save power.
- **straylight-display**: Display brightness is managed by power policy.
- **straylight-cron**: Tasks can be gated on AC power.
- **straylight-health**: Battery health contributes to the system health score.
- **straylight-network**: Wi-Fi power saving is managed by power profiles.

## SEE ALSO

straylight-thermal(1), straylight-autotune(1), straylight-display(1), straylight-cron(1), straylight-health(1)
