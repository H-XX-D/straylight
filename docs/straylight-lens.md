# straylight-lens

## NAME

straylight-lens -- full-stack tracing that correlates user-space, kernel, and network events

## SYNOPSIS

```
straylight-lens [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-lens provides distributed tracing across the entire system stack. It correlates events from application-level spans (OpenTelemetry), kernel tracepoints (eBPF), syscall traces, network packet captures, and GPU command queues into a single unified timeline. This makes it possible to follow a request from the moment it arrives on the network through kernel processing, application logic, disk I/O, and GPU compute, all the way to the response.

Where straylight-trace focuses on single-process instrumentation, lens operates at the system level, stitching together events from multiple processes, services, and even multiple nodes in a cluster. It assigns a correlation ID to each logical request and propagates it across process boundaries using the StrayLight bus.

lens stores traces in a compressed columnar format optimized for time-range queries and produces output compatible with Jaeger, Zipkin, and Chrome's trace viewer.

## COMMANDS

### `capture`

Start capturing a full-stack trace.

```
straylight-lens capture [--duration <duration>] [--filter <expression>] [--output <path>]
```

### `analyze`

Analyze a captured trace.

```
straylight-lens analyze <trace-file> [--focus <span-id>] [--critical-path]
```

### `live`

Stream trace events in real time.

```
straylight-lens live [--filter <expression>] [--depth <n>]
```

### `correlate`

Find all events related to a specific request or correlation ID.

```
straylight-lens correlate <correlation-id>
```

### `export`

Export a trace in standard formats.

```
straylight-lens export <trace-file> --format <jaeger|zipkin|chrome|json>
```

### `diff`

Compare two traces to identify performance differences.

```
straylight-lens diff <trace-a> <trace-b> [--threshold <ms>]
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--depth <n>` | Maximum stack depth to capture |
| `--buffer-size <bytes>` | Per-CPU ring buffer size |
| `--json` | JSON output |
| `--verbose` | Debug logging |

## CONFIGURATION

```toml
[lens]
store_path = "/var/lib/straylight/lens"
buffer_size = "128M"
max_trace_duration = "5m"

[sources]
kernel = true
userspace = true
network = true
gpu = true

[correlation]
propagation = "bus"         # bus | headers | both
id_format = "uuid"

[export]
default_format = "chrome"
```

## EXAMPLES

Capture a 30-second full-stack trace:

```
straylight-lens capture --duration 30s --output trace.lens
```

Follow the critical path of a request:

```
straylight-lens analyze trace.lens --critical-path
```

Find everything related to a specific request:

```
straylight-lens correlate req-4821-abcd
```

Export for Chrome's trace viewer:

```
straylight-lens export trace.lens --format chrome > trace.json
```

Compare performance before and after a change:

```
straylight-lens diff baseline.lens optimized.lens --threshold 5
```

Live stream with a filter:

```
straylight-lens live --filter "duration > 100ms" --depth 5
```

## INTEGRATION

- **straylight-trace**: Lens builds on trace's per-process probes for system-wide correlation.
- **straylight-flux**: Trace events can be ingested as flux streams for aggregation.
- **straylight-alice**: Performance anomalies detected by alice can be investigated with lens.
- **straylight-mesh**: Distributed GPU traces span multiple nodes via mesh coordination.
- **straylight-replay**: Flight recorder data can be imported into lens for analysis.

## SEE ALSO

straylight-trace(1), straylight-flux(1), straylight-alice(1), straylight-replay(1), straylight-mesh(1)
