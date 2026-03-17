# straylight-log

## NAME

straylight-log -- structured log viewer with filtering, correlation, and live tail

## SYNOPSIS

```
straylight-log [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-log is the primary interface for viewing and querying system logs on StrayLight OS. It reads from the StrayLight journal, which stores all log entries as structured JSON with indexed fields for fast querying. Unlike traditional syslog viewers, straylight-log understands the relationships between log entries and can correlate events across services, tools, and time.

The tool supports real-time tailing with filters, time-range queries, field-based filtering, full-text search, and log aggregation. Output can be formatted as human-readable tables, raw JSON, or compact one-liners for piping to other tools.

straylight-log can also consume flux streams for live monitoring of derived event streams, bridging the gap between traditional logging and real-time event processing.

## COMMANDS

### `show`

Display log entries.

```
straylight-log show [--since <datetime>] [--until <datetime>] [--unit <service>] [--level <level>]
```

### `tail`

Follow new log entries in real time.

```
straylight-log tail [--filter <expression>] [--unit <service>]
```

### `search`

Full-text search across logs.

```
straylight-log search <query> [--since <datetime>] [--limit <n>]
```

### `correlate`

Find related log entries by correlation ID or time proximity.

```
straylight-log correlate <entry-id|correlation-id>
```

### `stats`

Show log statistics.

```
straylight-log stats [--period <duration>] [--group-by <unit|level>]
```

### `export`

Export log entries.

```
straylight-log export [--since <datetime>] --format <json|csv|text> --output <path>
```

### `clean`

Remove old log entries.

```
straylight-log clean [--older-than <duration>] [--dry-run]
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--level <level>` | Filter by log level (debug, info, warning, error, critical) |
| `--unit <service>` | Filter by service/tool name |
| `--json` | JSON output |
| `--no-pager` | Do not pipe through a pager |
| `--verbose` | Debug logging |

## CONFIGURATION

```toml
[log]
journal_path = "/var/log/straylight/journal"
default_limit = 100
retention = "90d"

[display]
time_format = "relative"
color = true
show_fields = ["timestamp", "level", "unit", "message"]

[tail]
highlight_errors = true
sound_on_critical = false
```

## EXAMPLES

Show the last 50 error-level entries:

```
straylight-log show --level error --limit 50
```

Tail logs from a specific service:

```
straylight-log tail --unit straylight-alice
```

Search for disk-related errors in the past 24 hours:

```
straylight-log search "disk error" --since "24h ago"
```

Correlate related events:

```
straylight-log correlate req-4821-abcd
```

Export today's logs as JSON:

```
straylight-log export --since today --format json --output today.json
```

Log statistics by level:

```
straylight-log stats --period 7d --group-by level
```

## INTEGRATION

- **straylight-flux**: Log entries can be consumed as flux streams for real-time analysis.
- **straylight-timeline**: Significant log events appear in the activity timeline.
- **straylight-alice**: Error patterns in logs feed the AI anomaly detector.
- **straylight-dash / straylight-hub**: Log panels display live log streams.
- **straylight-lens**: Logs are correlated with trace spans for debugging.
- **straylight-replay**: Log entries are included in flight recorder captures.

## SEE ALSO

straylight-flux(1), straylight-timeline(1), straylight-lens(1), straylight-replay(1), straylight-alice(1)
