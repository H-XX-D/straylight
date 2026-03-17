# straylight-trace

## NAME

straylight-trace -- application tracer using eBPF and dynamic probes

## SYNOPSIS

```
straylight-trace [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-trace instruments applications at runtime to capture function calls, syscalls, memory allocations, lock contention, and I/O operations. It uses eBPF programs for zero-overhead kernel-side tracing and dynamic probes (uprobes, USDT) for user-space instrumentation, all without requiring application modification or recompilation.

trace is designed for production debugging. Its overhead is typically under 2% for targeted tracing and under 5% even for comprehensive instrumentation. Traces are captured in a compact binary format and can be analyzed offline or streamed to straylight-lens for system-wide correlation.

The tool includes pre-built tracing recipes for common scenarios: memory leak detection, lock contention analysis, I/O latency profiling, and CPU flame graph generation. Custom probes can be defined in a simple DSL or written directly in BPF C.

## COMMANDS

### `attach`

Attach probes to a running process.

```
straylight-trace attach <pid> [--recipe <name>] [--probes <list>]
```

### `run`

Launch a command with tracing enabled.

```
straylight-trace run [--recipe <name>] -- <command> [args...]
```

### `record`

Record a trace session to a file.

```
straylight-trace record <pid> --output <path> [--duration <duration>]
```

### `report`

Generate a report from a trace recording.

```
straylight-trace report <trace-file> [--type <flamegraph|histogram|timeline>]
```

### `list`

List available recipes and probes.

```
straylight-trace list [--recipes] [--probes <pid>]
```

### `live`

Stream trace events to the terminal.

```
straylight-trace live <pid> [--filter <expression>]
```

### `detach`

Remove probes from a process.

```
straylight-trace detach <pid>
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--recipe <name>` | Pre-built tracing recipe |
| `--stack-depth <n>` | Maximum stack trace depth (default: 32) |
| `--json` | JSON output |
| `--verbose` | Debug logging |

## CONFIGURATION

```toml
[trace]
store_path = "/var/lib/straylight/trace"
default_stack_depth = 32
buffer_size = "64M"

[recipes]
path = "/usr/share/straylight/trace/recipes"

[symbols]
debug_info = true
demangle = true
source_lines = true
```

## EXAMPLES

Generate a CPU flame graph:

```
straylight-trace record 4821 --output app.trace --duration 30s
straylight-trace report app.trace --type flamegraph > flame.svg
```

Detect memory leaks:

```
straylight-trace attach 4821 --recipe memory-leaks
```

Profile I/O latency:

```
straylight-trace run --recipe io-latency -- ./my-app
```

Live stream syscalls:

```
straylight-trace live 4821 --filter "syscall"
```

List available probes for a binary:

```
straylight-trace list --probes 4821
```

## INTEGRATION

- **straylight-lens**: Trace data feeds into full-stack distributed tracing.
- **straylight-rewind**: Traces can be correlated with process checkpoints.
- **straylight-alice**: Performance anomalies trigger automatic trace capture.
- **straylight-dash**: Trace summaries appear in the dashboard process panel.
- **straylight-fuse**: Traces work transparently across fused processes.

## SEE ALSO

straylight-lens(1), straylight-rewind(1), straylight-replay(1), straylight-alice(1), straylight-bench(1)
