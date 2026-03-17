# straylight-flux

## NAME

straylight-flux -- realtime stream processor for structured and unstructured event data

## SYNOPSIS

```
straylight-flux [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-flux is a lightweight stream processing engine built into StrayLight OS. It ingests events from the StrayLight bus, kernel tracepoints, log files, and external sources, then applies user-defined transformations, filters, and aggregations in real time. Results can be routed to files, dashboards, alerts, or other flux pipelines.

flux uses a SQL-like query language called FQL (Flux Query Language) that makes it easy to express complex event processing logic without writing code. Queries can join multiple streams, compute windowed aggregations, detect patterns across events, and emit derived events back onto the bus.

The tool is designed for low-latency, low-overhead operation. It processes millions of events per second on a single core by leveraging zero-copy buffers and SIMD-accelerated parsing. flux is the event backbone that powers straylight-timeline, straylight-alice's real-time detection, and straylight-lens's correlation engine.

## COMMANDS

### `run`

Execute a flux query.

```
straylight-flux run <query.fql>
straylight-flux run --inline "<fql-expression>"
```

### `stream`

Subscribe to a named stream and print events.

```
straylight-flux stream <stream-name> [--filter <fql-where>] [--limit <n>]
```

### `create`

Create a named persistent stream.

```
straylight-flux create <stream-name> --source <source> [--transform <fql>]
```

### `list`

List active streams and pipelines.

```
straylight-flux list [--json]
```

### `stats`

Show processing statistics.

```
straylight-flux stats [--stream <name>]
```

### `replay`

Replay historical events through a pipeline.

```
straylight-flux replay --source <archive> --query <query.fql> [--speed <multiplier>]
```

### `validate`

Check FQL syntax without executing.

```
straylight-flux validate <query.fql>
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--json` | JSON output |
| `--buffer-size <bytes>` | Ring buffer size per stream |
| `--workers <n>` | Number of processing threads |
| `--verbose` | Debug logging |

## CONFIGURATION

```toml
[flux]
buffer_size = "64M"
workers = 4
store_path = "/var/lib/straylight/flux"

[sources]
bus = true
journal = true
tracepoints = false

[retention]
default = "24h"
max_disk = "10G"
```

FQL example:

```sql
SELECT source, count(*) as errors, window_end
FROM bus_events
WHERE level = 'error'
GROUP BY source, TUMBLE(timestamp, INTERVAL '1 minute')
HAVING errors > 10
EMIT TO 'alert-stream'
```

## EXAMPLES

Count errors per minute from the bus:

```
straylight-flux run --inline "SELECT source, count(*) FROM bus WHERE level='error' GROUP BY source, TUMBLE(ts, '1m')"
```

Stream all disk events:

```
straylight-flux stream disk-events --limit 100
```

Create a persistent aggregation pipeline:

```
straylight-flux create cpu-summary --source bus --transform "SELECT avg(usage) FROM cpu_metrics GROUP BY core, TUMBLE(ts, '10s')"
```

Replay yesterday's events at 10x speed:

```
straylight-flux replay --source /var/lib/straylight/flux/2026-03-15 --query analysis.fql --speed 10
```

Validate a query file:

```
straylight-flux validate my-pipeline.fql
```

## INTEGRATION

- **straylight-timeline**: Timeline events are stored via flux streams.
- **straylight-alice**: AI anomaly detection consumes flux aggregation streams.
- **straylight-lens**: Distributed tracing correlation uses flux joins.
- **straylight-pipe**: Visual dataflow editor generates FQL queries.
- **straylight-log**: Log viewer can tap into flux streams.

## SEE ALSO

straylight-pipe(1), straylight-timeline(1), straylight-alice(1), straylight-lens(1), straylight-log(1)
