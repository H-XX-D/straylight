# straylight-health

## NAME

straylight-health -- unified health monitor aggregating metrics from all subsystems

## SYNOPSIS

```
straylight-health [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-health is the single source of truth for system health on StrayLight OS. It aggregates status reports from every subsystem -- CPU, memory, disk, network, GPU, thermal, security, and services -- and computes a unified health score. The score ranges from 0 (critical) to 100 (perfect) and is decomposed into per-subsystem ratings.

The health monitor runs continuously as a system service, publishing its assessments to the StrayLight bus every 10 seconds. Other tools query health status to make operational decisions: straylight-cron gates task execution on health, straylight-swarm uses it for workload placement, and straylight-alice feeds it into the AI anomaly model.

straylight-health also performs active checks: it verifies that filesystems are mounted, services are running, certificates are valid, and SMART diagnostics are passing. Failed checks generate structured alerts that flow through straylight-notify.

## COMMANDS

### `check`

Run health checks and report results.

```
straylight-health check [--full] [--subsystem <name>] [--json]
```

### `status`

Show the current health score.

```
straylight-health status [--json] [--verbose]
```

### `watch`

Continuously monitor health.

```
straylight-health watch [--interval <duration>]
```

### `history`

Show health score history.

```
straylight-health history [--since <datetime>] [--format <text|json|csv>]
```

### `report`

Generate a detailed health report.

```
straylight-health report [--output <path>] [--format <html|json|text>]
```

### `define`

Add a custom health check.

```
straylight-health define <check.toml>
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--json` | JSON output |
| `--threshold <score>` | Exit with code 1 if health is below this score |
| `--verbose` | Debug logging |

## CONFIGURATION

```toml
[health]
check_interval = "10s"
history_retention = "90d"

[scoring]
weights = { cpu = 15, memory = 15, disk = 20, network = 10, gpu = 10, thermal = 10, security = 10, services = 10 }

[thresholds]
warning = 70
critical = 40

[checks]
smart = true
certificates = true
services = true
filesystems = true

[alerts]
delivery = ["bus", "notify"]
```

## EXAMPLES

Quick health check:

```
straylight-health status
```

Full health check with details:

```
straylight-health check --full --verbose
```

Generate an HTML health report:

```
straylight-health report --format html --output /tmp/health.html
```

Exit with error if health is below 70 (useful in scripts):

```
straylight-health status --threshold 70
```

Watch health continuously:

```
straylight-health watch --interval 5s
```

Show health history as CSV:

```
straylight-health history --since "7d ago" --format csv
```

## INTEGRATION

- **straylight-alice**: AI model uses health scores as a primary input signal.
- **straylight-cron**: Tasks can be gated on minimum health scores.
- **straylight-swarm**: Cluster orchestration uses per-node health for placement decisions.
- **straylight-notify**: Health alerts flow through the notification daemon.
- **straylight-hub / straylight-dash**: Both dashboards display health prominently.
- **straylight-thermal**: Thermal status is a component of the health score.

## SEE ALSO

straylight-alice(1), straylight-dash(1), straylight-hub(1), straylight-thermal(1), straylight-notify(1)
