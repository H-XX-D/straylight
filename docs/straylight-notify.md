# straylight-notify

## NAME

straylight-notify -- notification daemon with priority routing, grouping, and do-not-disturb

## SYNOPSIS

```
straylight-notify [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-notify is the system notification daemon for StrayLight OS. It receives notifications from all StrayLight tools, system services, and applications, then routes them through a priority-based pipeline that handles grouping, deduplication, rate limiting, and delivery to the appropriate output channel (desktop popup, terminal bell, sound, email, or webhook).

Notifications are classified into priority levels: critical (requires immediate attention), high (should be seen soon), normal (informational), and low (can be batched). Critical notifications bypass do-not-disturb mode. Notifications from the same source with the same type are automatically grouped to prevent flooding.

The tool supports do-not-disturb scheduling, per-application notification preferences, and notification history with search. Users can define custom routing rules that send specific notifications to specific channels (e.g., send all security alerts to a Slack webhook).

## COMMANDS

### `send`

Send a notification.

```
straylight-notify send --title <title> --body <body> [--priority <level>] [--icon <path>] [--attach <file>]
```

### `list`

Show notification history.

```
straylight-notify list [--since <datetime>] [--priority <level>] [--json]
```

### `clear`

Clear notifications.

```
straylight-notify clear [--all] [--id <id>]
```

### `dnd`

Manage do-not-disturb.

```
straylight-notify dnd [on|off] [--duration <duration>] [--allow-critical]
```

### `rules`

Manage notification routing rules.

```
straylight-notify rules list
straylight-notify rules add --source <pattern> --channel <channel> [--priority <level>]
straylight-notify rules remove <rule-id>
```

### `channels`

Manage notification channels.

```
straylight-notify channels list
straylight-notify channels add <name> --type <desktop|email|webhook|sound>
straylight-notify channels test <name>
```

### `preferences`

Manage per-application preferences.

```
straylight-notify preferences show [<app>]
straylight-notify preferences set <app> --priority <level> [--mute]
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--json` | JSON output |
| `--quiet` | Suppress output |
| `--verbose` | Debug logging |

## CONFIGURATION

```toml
[notify]
default_channel = "desktop"
history_retention = "30d"
group_timeout = "10s"
rate_limit = 30             # max notifications per minute

[dnd]
schedule = [{ start = "22:00", end = "08:00" }]
allow_critical = true

[channels.desktop]
type = "desktop"
position = "top-right"
timeout = "5s"

[channels.email]
type = "email"
address = "admin@example.com"
min_priority = "high"

[channels.webhook]
type = "webhook"
url = "https://hooks.slack.com/services/..."
min_priority = "critical"
```

## EXAMPLES

Send a notification:

```
straylight-notify send --title "Build Complete" --body "Project compiled successfully" --priority normal
```

Enable do-not-disturb for 2 hours:

```
straylight-notify dnd on --duration 2h --allow-critical
```

Route security alerts to webhook:

```
straylight-notify rules add --source "straylight-shield" --channel webhook --priority high
```

Show recent critical notifications:

```
straylight-notify list --priority critical --since "24h ago"
```

Mute notifications from a specific app:

```
straylight-notify preferences set "Chromium" --mute
```

Test a notification channel:

```
straylight-notify channels test webhook
```

## INTEGRATION

- **straylight-alice**: AI anomaly alerts are delivered through notify.
- **straylight-health**: Health status changes trigger notifications.
- **straylight-cron**: Task success/failure notifications.
- **straylight-shield**: Security alerts use critical priority.
- **straylight-update**: Update availability and completion notifications.
- **straylight-audio**: Notification sounds are routed through the audio system.

## SEE ALSO

straylight-alice(1), straylight-health(1), straylight-cron(1), straylight-shield(1), straylight-audio(1)
