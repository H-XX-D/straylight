# straylight-bench

## NAME

straylight-bench -- hardware and software benchmark suite for StrayLight OS

## SYNOPSIS

```
straylight-bench [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-bench is a unified benchmarking framework that measures the performance of every major system component: CPU, memory, storage, network, and GPU. It produces reproducible, comparable results by controlling for thermal state, background load, and kernel tuning before each run.

Each benchmark runs in an isolated cgroup to prevent interference from other workloads. Results are stored in a local database so that performance can be tracked over time and regressions detected automatically. straylight-bench can also compare results against a community baseline for the same hardware, downloaded from the StrayLight telemetry service.

The tool supports both quick single-subsystem tests for spot-checking and full-suite runs suitable for hardware validation after upgrades or kernel changes.

## COMMANDS

### `run`

Execute one or more benchmarks.

```
straylight-bench run [<suite>...] [--iterations <n>] [--warmup <n>]
```

Available suites: `cpu`, `memory`, `disk`, `network`, `gpu`, `all`. Multiple suites can be listed. Default is `all`.

### `list`

List available benchmark suites and individual tests.

```
straylight-bench list [--suite <name>]
```

### `compare`

Compare two result sets.

```
straylight-bench compare <run-id-a> <run-id-b> [--threshold <pct>]
```

Highlights regressions and improvements exceeding the threshold.

### `history`

Show historical benchmark results.

```
straylight-bench history [--suite <name>] [--since <datetime>] [--format <text|json|csv>]
```

### `baseline`

Download or set the community baseline for this hardware.

```
straylight-bench baseline [--download] [--set <run-id>]
```

### `export`

Export results in standard formats.

```
straylight-bench export <run-id> --format <json|csv|html>
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--iterations <n>` | Number of iterations per test (default: 3) |
| `--warmup <n>` | Warmup iterations before measurement (default: 1) |
| `--isolate` | Run in maximum isolation (disable HT, pin cores) |
| `--json` | JSON output |
| `--quiet` | Only print final scores |
| `--tag <label>` | Tag a run for easy reference |

## CONFIGURATION

```toml
[bench]
default_iterations = 3
warmup = 1
store_path = "/var/lib/straylight/bench"

[cpu]
tests = ["integer", "float", "simd", "crypto", "compression"]

[memory]
tests = ["bandwidth", "latency", "random_access"]

[disk]
tests = ["sequential_read", "sequential_write", "random_4k", "fsync"]
test_file_size = "1G"

[gpu]
tests = ["compute", "bandwidth", "inference"]

[network]
tests = ["throughput", "latency", "connections_per_sec"]
target = "localhost"
```

## EXAMPLES

Run all benchmarks with 5 iterations:

```
straylight-bench run all --iterations 5
```

Quick CPU-only check:

```
straylight-bench run cpu --quiet
```

Compare before and after a kernel upgrade:

```
straylight-bench compare run-20260301 run-20260315
```

Export the latest run as HTML:

```
straylight-bench export latest --format html > bench-report.html
```

Track disk performance over time:

```
straylight-bench history --suite disk --since "30d ago" --format csv
```

## INTEGRATION

- **straylight-autotune**: Benchmark results inform tuning profile selection.
- **straylight-alice**: Performance regressions detected by bench are fed to the AI monitor.
- **straylight-thermal**: Bench waits for thermal equilibrium before measuring.
- **straylight-capsule**: Benchmarks respect resource contracts to avoid stealing quota.
- **straylight-timeline**: Benchmark events are recorded in the activity timeline.

## SEE ALSO

straylight-autotune(1), straylight-alice(1), straylight-thermal(1), straylight-trace(1)
