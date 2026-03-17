# straylight-dash

## NAME

straylight-dash -- TUI system dashboard for terminal-first operators

## SYNOPSIS

```
straylight-dash [OPTIONS] [LAYOUT]
```

## DESCRIPTION

straylight-dash provides a full-featured terminal user interface for monitoring and managing StrayLight OS. It renders live-updating panels for CPU, memory, disk, network, GPU, and thermal status using Unicode block characters and 24-bit color. The dashboard is designed for operators who prefer a terminal workflow over graphical tools.

The layout is fully customizable. Users can define which panels appear, their size and position, refresh rates, and alert thresholds. Layouts are stored as TOML files and can be switched on the fly. straylight-dash ships with several preset layouts: `overview`, `network`, `storage`, `gpu`, and `minimal`.

straylight-dash is a read-mostly tool, but it also supports interactive actions: selecting a process to send a signal, drilling into a specific disk or network interface, or jumping directly to straylight-trace for a highlighted process.

## COMMANDS

### `open`

Launch the dashboard (default command).

```
straylight-dash [open] [--layout <name>]
```

### `layouts`

List available layouts.

```
straylight-dash layouts
```

### `screenshot`

Capture the current dashboard state to a file.

```
straylight-dash screenshot [--output <path>] [--format <png|text|ansi>]
```

### `record`

Record a dashboard session for playback.

```
straylight-dash record --output <path> [--duration <duration>]
```

### `playback`

Replay a recorded session.

```
straylight-dash playback <recording>
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--layout <name>` | Layout preset or path to custom layout TOML |
| `--refresh <ms>` | Global refresh interval in milliseconds (default: 1000) |
| `--no-color` | Disable color output |
| `--compact` | Use a single-column compact layout |
| `--mouse` | Enable mouse interaction |

## CONFIGURATION

```toml
[dash]
default_layout = "overview"
refresh_ms = 1000
mouse = true
color_scheme = "straylight-dark"

[panels.cpu]
enabled = true
position = { row = 0, col = 0, width = 2, height = 1 }
show_per_core = true

[panels.memory]
enabled = true
position = { row = 0, col = 2, width = 1, height = 1 }

[panels.network]
enabled = true
position = { row = 1, col = 0, width = 3, height = 1 }
interfaces = ["eth0", "wlan0"]

[panels.alerts]
enabled = true
position = { row = 2, col = 0, width = 3, height = 1 }
max_lines = 10
```

## EXAMPLES

Launch with the default layout:

```
straylight-dash
```

Use the GPU-focused layout:

```
straylight-dash --layout gpu
```

Compact mode for small terminals:

```
straylight-dash --compact --refresh 2000
```

Capture a text screenshot:

```
straylight-dash screenshot --format text --output status.txt
```

Record 5 minutes for later review:

```
straylight-dash record --output session.rec --duration 5m
```

## INTEGRATION

- **straylight-hub**: dash is the TUI counterpart to hub's graphical dashboard.
- **straylight-alice**: Alert panels display alice anomaly alerts in real time.
- **straylight-trace**: Drill-down from a process panel opens the tracer.
- **straylight-health**: Health status is displayed as a top-level indicator.
- **straylight-thermal**: Thermal panels show zone temperatures and fan speeds.

## SEE ALSO

straylight-hub(1), straylight-health(1), straylight-alice(1), straylight-trace(1), straylight-thermal(1)
