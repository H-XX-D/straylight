# straylight-quota

## NAME

straylight-quota -- resource budget manager with per-user and per-application cgroup limits

## SYNOPSIS

```
straylight-quota [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-quota manages resource budgets at the user, group, and application level. It defines how much CPU time, memory, disk I/O, and network bandwidth each entity is permitted to consume, and enforces those limits through cgroups v2. Quotas prevent any single user or application from monopolizing system resources.

Quotas can be defined as hard limits (exceeding them triggers throttling or OOM), soft limits (exceeding them generates warnings), or burst limits (temporary overage is allowed up to a cap). The system supports hierarchical budgets: a team quota can be divided among team members, and each member's quota can be further divided among their applications.

straylight-quota integrates with straylight-capsule for application-level contracts and with straylight-users for identity resolution.

## COMMANDS

### `set`

Set a quota for a user, group, or application.

```
straylight-quota set <entity> --cpu <shares> --memory <limit> [--io <bandwidth>] [--net <bandwidth>]
```

### `get`

Show quotas for an entity.

```
straylight-quota get <entity> [--json]
```

### `list`

List all configured quotas.

```
straylight-quota list [--json] [--type <user|group|app>]
```

### `usage`

Show current resource usage against quotas.

```
straylight-quota usage [<entity>] [--json]
```

### `remove`

Remove a quota.

```
straylight-quota remove <entity>
```

### `report`

Generate a usage report.

```
straylight-quota report [--period <duration>] [--format <text|json|csv>]
```

### `alert`

Configure quota alerts.

```
straylight-quota alert <entity> --threshold <pct> --action <notify|throttle|kill>
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--json` | JSON output |
| `--verbose` | Debug logging |

## CONFIGURATION

```toml
[quota]
enforcement = "hard"        # hard | soft | burst
default_cpu = 1024
default_memory = "4G"

[burst]
allowed = true
max_duration = "5m"
overage_ratio = 1.5

[hierarchy]
inherit = true
```

## EXAMPLES

Set a user quota:

```
straylight-quota set user:alice --cpu 2048 --memory 16G --io 500M
```

Check usage against quotas:

```
straylight-quota usage user:alice
```

Set a group quota:

```
straylight-quota set group:developers --cpu 8192 --memory 64G
```

Generate a weekly usage report:

```
straylight-quota report --period 7d --format csv > usage.csv
```

Alert when usage exceeds 80%:

```
straylight-quota alert user:alice --threshold 80 --action notify
```

## INTEGRATION

- **straylight-capsule**: Application capsules declare resources within quota limits.
- **straylight-users**: User identity determines which quota applies.
- **straylight-autotune**: Global tuning respects quota cgroup hierarchy.
- **straylight-health**: Quota violations contribute to the health score.
- **straylight-notify**: Alerts are delivered through the notification daemon.

## SEE ALSO

straylight-capsule(1), straylight-users(1), straylight-autotune(1), straylight-notify(1), straylight-fuse(1)
