# straylight-replay

## NAME

straylight-replay -- flight recorder that captures system events for post-mortem analysis

## SYNOPSIS

```
straylight-replay [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-replay continuously records system events into a circular buffer, similar to an aircraft flight recorder. When a problem occurs, the recording can be frozen and analyzed to understand exactly what happened in the seconds or minutes leading up to the incident. This eliminates the need to reproduce problems -- the evidence is already captured.

The recorder captures kernel events (syscalls, scheduler decisions, I/O completions, network packets), application events (from instrumented code), and StrayLight bus messages. The circular buffer is sized to hold a configurable window of history, typically 5 to 30 minutes, and the overhead is low enough for always-on recording in production.

replay stores recordings in a compact binary format that can be exported for offline analysis, shared with support teams, or imported into straylight-lens for full-stack visualization.

## COMMANDS

### `start`

Start the flight recorder.

```
straylight-replay start [--buffer-size <size>] [--window <duration>]
```

### `stop`

Stop recording and optionally save.

```
straylight-replay stop [--save <path>]
```

### `freeze`

Freeze the current buffer for analysis (recording continues in a new buffer).

```
straylight-replay freeze [--output <path>] [--reason <text>]
```

### `view`

Open a recording for interactive analysis.

```
straylight-replay view <recording> [--from <timestamp>] [--to <timestamp>]
```

### `search`

Search recordings for specific events.

```
straylight-replay search <recording> --pattern <expression>
```

### `export`

Export a recording in standard formats.

```
straylight-replay export <recording> --format <lens|pcap|json> --output <path>
```

### `status`

Show recorder status and buffer utilization.

```
straylight-replay status [--json]
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--buffer-size <size>` | Ring buffer size (default: 256M) |
| `--window <duration>` | Time window to retain (default: 10m) |
| `--json` | JSON output |
| `--verbose` | Debug logging |

## CONFIGURATION

```toml
[replay]
auto_start = true
buffer_size = "256M"
window = "10m"
store_path = "/var/lib/straylight/replay"

[capture]
syscalls = true
scheduler = true
network = true
disk_io = true
bus_events = true

[triggers]
# Auto-freeze on certain conditions
on_panic = true
on_oom = true
on_service_crash = true

[retention]
max_recordings = 50
max_disk = "5G"
```

## EXAMPLES

Start recording with a 30-minute window:

```
straylight-replay start --window 30m
```

Freeze the buffer after a crash:

```
straylight-replay freeze --reason "service crash at 14:32"
```

View the last 5 minutes of a recording:

```
straylight-replay view recording-20260315.rep --from "-5m"
```

Search for OOM events:

```
straylight-replay search recording-20260315.rep --pattern "oom_kill"
```

Export for lens analysis:

```
straylight-replay export recording-20260315.rep --format lens --output incident.lens
```

## INTEGRATION

- **straylight-lens**: Recordings can be imported into lens for full-stack trace analysis.
- **straylight-alice**: AI anomalies can trigger automatic buffer freezes.
- **straylight-rewind**: Process checkpoints complement replay's system-level recording.
- **straylight-health**: Replay auto-freezes on health score drops below critical threshold.
- **straylight-timeline**: Frozen recordings are logged as timeline events.

## SEE ALSO

straylight-lens(1), straylight-rewind(1), straylight-trace(1), straylight-alice(1), straylight-timeline(1)
