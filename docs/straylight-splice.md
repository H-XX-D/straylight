# straylight-splice

## NAME

straylight-splice -- zero-copy pipeline builder using kernel splice and io_uring

## SYNOPSIS

```
straylight-splice [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-splice constructs data pipelines that move data between files, sockets, and pipes without copying it through user-space memory. It leverages the Linux kernel's splice(2), tee(2), and vmsplice(2) syscalls, augmented with io_uring for asynchronous batched submissions, to achieve near-wire-speed data transfer.

Traditional shell pipelines copy data from kernel space to user space and back at each stage. For I/O-heavy workloads (log processing, media transcoding, database ETL), this copying can consume significant CPU and memory bandwidth. splice eliminates the copies by keeping data in kernel pipe buffers and transferring it directly between file descriptors.

splice provides a declarative pipeline syntax where each stage is either a file, socket, command, or transformation. It automatically selects the optimal data transfer method (splice, sendfile, io_uring) based on the file descriptor types involved.

## COMMANDS

### `run`

Execute a splice pipeline.

```
straylight-splice run <pipeline.toml>
straylight-splice run --inline "<source> | <transform> | <sink>"
```

### `bench`

Benchmark a pipeline against a traditional pipe equivalent.

```
straylight-splice bench <pipeline.toml>
```

### `analyze`

Analyze a pipeline and show the transfer methods that will be used.

```
straylight-splice analyze <pipeline.toml>
```

### `monitor`

Show live throughput for running pipelines.

```
straylight-splice monitor [<pipeline-id>] [--json]
```

### `list`

List running pipelines.

```
straylight-splice list [--json]
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--buffer-size <size>` | Pipe buffer size (default: 1M) |
| `--io-uring` | Force io_uring mode |
| `--json` | JSON output |
| `--verbose` | Debug logging |

## CONFIGURATION

```toml
[splice]
default_buffer_size = "1M"
use_io_uring = true
max_pipelines = 64

[io_uring]
ring_size = 256
sqpoll = true
```

Pipeline format:

```toml
[pipeline]
name = "log-processor"

[[stage]]
type = "file"
path = "/var/log/straylight/events.log"
mode = "read"

[[stage]]
type = "filter"
command = "grep -v DEBUG"

[[stage]]
type = "file"
path = "/var/log/straylight/events-filtered.log"
mode = "write"
```

## EXAMPLES

Zero-copy file-to-socket transfer:

```
straylight-splice run --inline "file:/var/log/big.log | socket:tcp:backup-host:9500"
```

Benchmark a pipeline:

```
straylight-splice bench log-pipeline.toml
```

Analyze transfer methods:

```
straylight-splice analyze log-pipeline.toml
```

Monitor throughput:

```
straylight-splice monitor --json
```

High-throughput file copy:

```
straylight-splice run --inline "file:/data/source.img | file:/data/dest.img"
```

## INTEGRATION

- **straylight-fuse**: Splice handles I/O optimization; fuse handles process-level merging.
- **straylight-flux**: Flux streams can use splice for high-throughput event ingestion.
- **straylight-pipe**: Pipeline nodes can use splice for zero-copy data movement.
- **straylight-mirror**: Mirror uses splice for efficient block-level replication.
- **straylight-bench**: Benchmarks measure splice vs. traditional pipeline throughput.

## SEE ALSO

straylight-fuse(1), straylight-flux(1), straylight-pipe(1), straylight-mirror(1), straylight-bench(1)
