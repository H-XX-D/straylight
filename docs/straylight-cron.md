# straylight-cron

## NAME

straylight-cron -- smart task scheduler with dependency awareness and resource gating

## SYNOPSIS

```
straylight-cron [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-cron replaces traditional cron with a scheduler that understands task dependencies, resource availability, and system health. Tasks can declare that they depend on other tasks completing successfully, that they require a minimum amount of free memory or CPU, or that they should only run when the system is idle.

The scheduler integrates with straylight-alice to make intelligent decisions about when to run maintenance tasks. A large backup job can be deferred until the AI monitor confirms the system is not under load, then executed at the optimal moment. Tasks can also declare power-awareness, refusing to run on battery or deferring to periods of AC power.

straylight-cron uses a TOML-based job definition format that is more readable than traditional crontab syntax while supporting all the same scheduling patterns plus event-driven triggers.

## COMMANDS

### `add`

Add a new scheduled task.

```
straylight-cron add <job.toml>
straylight-cron add --inline "<schedule>" -- <command>
```

### `remove`

Remove a scheduled task.

```
straylight-cron remove <job-name>
```

### `list`

List all scheduled tasks.

```
straylight-cron list [--active] [--pending] [--json]
```

### `run`

Manually trigger a task immediately.

```
straylight-cron run <job-name> [--force]
```

### `enable` / `disable`

Enable or disable a task without removing it.

```
straylight-cron enable <job-name>
straylight-cron disable <job-name>
```

### `log`

Show execution history for a task.

```
straylight-cron log <job-name> [--last <n>] [--failures]
```

### `next`

Show when a task will next execute.

```
straylight-cron next [<job-name>] [--count <n>]
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--json` | JSON output |
| `--user <name>` | Operate on a specific user's jobs |
| `--verbose` | Debug logging |

## CONFIGURATION

Job definition format:

```toml
[job]
name = "nightly-backup"
description = "Full system backup to NAS"
command = "straylight-snapshot create --target /mnt/nas"

[schedule]
cron = "0 3 * * *"          # Traditional cron syntax
jitter = "15m"               # Random delay up to 15 minutes

[conditions]
depends_on = ["cleanup-tmp"]
min_free_memory = "2G"
require_ac_power = true
require_idle = true
health_gate = "normal"       # Only run if alice says system is normal

[retry]
max_attempts = 3
backoff = "exponential"
delay = "5m"

[notify]
on_failure = true
on_success = false
```

## EXAMPLES

Add a job from a TOML file:

```
straylight-cron add /etc/straylight/jobs/nightly-backup.toml
```

Add a quick inline job:

```
straylight-cron add --inline "0 */6 * * *" -- straylight-health check --full
```

List pending jobs:

```
straylight-cron list --pending
```

Show next 5 scheduled executions:

```
straylight-cron next --count 5
```

View failure history for a job:

```
straylight-cron log nightly-backup --failures
```

Manually trigger a job:

```
straylight-cron run nightly-backup --force
```

## INTEGRATION

- **straylight-alice**: Tasks can be gated on AI health assessments.
- **straylight-power**: Power-aware scheduling defers tasks when on battery.
- **straylight-notify**: Sends notifications on task success or failure.
- **straylight-snapshot**: Commonly scheduled to create periodic snapshots.
- **straylight-timeline**: Task executions are logged to the activity timeline.
- **straylight-capsule**: Tasks can declare resource requirements as capsule contracts.

## SEE ALSO

straylight-alice(1), straylight-timeline(1), straylight-notify(1), straylight-capsule(1), straylight-power(1)
