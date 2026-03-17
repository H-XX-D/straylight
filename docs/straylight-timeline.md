# straylight-timeline

## NAME

straylight-timeline -- activity tracker that records system-wide events to a queryable log

## SYNOPSIS

```
straylight-timeline [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-timeline is the system-wide activity ledger for StrayLight OS. It records every significant event: tool invocations, configuration changes, package installations, user logins, service state transitions, hardware events, and manual annotations. Events are stored in a structured, indexed database that supports fast time-range queries and full-text search.

Unlike traditional log files that scatter information across dozens of locations, timeline provides a single chronological view of everything that happened on the system. This is invaluable for debugging ("what changed right before the system started misbehaving?") and auditing ("who did what and when?").

Events are tagged with metadata including the originating tool, user, severity, and category. timeline provides both a CLI query interface and a feed that straylight-dash and straylight-hub can display as a live activity stream.

## COMMANDS

### `show`

Display recent events.

```
straylight-timeline show [--since <datetime>] [--until <datetime>] [--limit <n>]
```

### `search`

Search for events matching a query.

```
straylight-timeline search <query> [--category <cat>] [--tool <name>] [--user <name>]
```

### `annotate`

Add a manual annotation to the timeline.

```
straylight-timeline annotate <message> [--severity <level>] [--tags <list>]
```

### `export`

Export events in standard formats.

```
straylight-timeline export [--since <datetime>] --format <json|csv|text> [--output <path>]
```

### `stats`

Show event statistics.

```
straylight-timeline stats [--period <duration>] [--group-by <tool|category|user>]
```

### `watch`

Stream new events in real time.

```
straylight-timeline watch [--filter <expression>]
```

### `prune`

Remove old events beyond the retention window.

```
straylight-timeline prune [--older-than <duration>] [--dry-run]
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--json` | JSON output |
| `--no-color` | Disable colored output |
| `--verbose` | Debug logging |

## CONFIGURATION

```toml
[timeline]
db_path = "/var/lib/straylight/timeline/events.db"
retention = "365d"
auto_prune = true

[capture]
tool_invocations = true
config_changes = true
user_sessions = true
hardware_events = true
service_events = true

[display]
default_limit = 50
time_format = "relative"    # relative | iso | local
```

## EXAMPLES

Show events from the last hour:

```
straylight-timeline show --since "1h ago"
```

Search for all disk-related events:

```
straylight-timeline search "disk" --category hardware
```

Add a manual annotation:

```
straylight-timeline annotate "Started database migration" --tags "migration,database"
```

Export today's events as JSON:

```
straylight-timeline export --since "today" --format json --output events.json
```

Watch events in real time:

```
straylight-timeline watch --filter "severity >= warning"
```

Event statistics grouped by tool:

```
straylight-timeline stats --period 7d --group-by tool
```

## INTEGRATION

- **All StrayLight tools**: Every tool logs its significant actions to timeline.
- **straylight-echo**: Undo operations are recorded as timeline events.
- **straylight-flux**: Timeline data can be consumed as a flux stream.
- **straylight-dash / straylight-hub**: Activity feed tiles display timeline events.
- **straylight-shield**: Security-relevant events are flagged for audit.

## SEE ALSO

straylight-log(1), straylight-echo(1), straylight-flux(1), straylight-dash(1), straylight-replay(1)
