# straylight-pipe

## NAME

straylight-pipe -- visual dataflow editor with a TUI and YAML backend

## SYNOPSIS

```
straylight-pipe [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-pipe provides a visual way to design data processing pipelines by connecting StrayLight tools into directed graphs. Each node in a pipeline represents a tool invocation or data transformation, and edges represent data flow between them. Pipelines are stored as YAML files that can be version-controlled and deployed.

pipe offers both a TUI-based visual editor (for terminal users) and a YAML-first workflow (for automation). In the TUI, users drag nodes, draw connections, and configure parameters interactively. The editor validates pipelines in real time, checking that output types match input types and that all required parameters are set.

Pipelines can be run directly by pipe or deployed as straylight-cron jobs. pipe handles parallelism, error recovery, and progress reporting automatically. Failed nodes can be retried individually without re-running the entire pipeline.

## COMMANDS

### `edit`

Open the visual pipeline editor.

```
straylight-pipe edit [<pipeline.yaml>]
```

### `run`

Execute a pipeline.

```
straylight-pipe run <pipeline.yaml> [--parallel <n>] [--resume]
```

### `validate`

Check a pipeline for errors.

```
straylight-pipe validate <pipeline.yaml>
```

### `list`

List saved pipelines.

```
straylight-pipe list [--json]
```

### `show`

Display a pipeline as a graph.

```
straylight-pipe show <pipeline.yaml> [--format <ascii|dot|json>]
```

### `status`

Check the status of a running pipeline.

```
straylight-pipe status <run-id>
```

### `retry`

Retry failed nodes in a pipeline run.

```
straylight-pipe retry <run-id> [--node <name>]
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--parallel <n>` | Maximum parallel nodes |
| `--dry-run` | Validate and plan without executing |
| `--json` | JSON output |
| `--verbose` | Debug logging |

## CONFIGURATION

Pipeline YAML format:

```yaml
name: daily-report
description: Generate and distribute daily system report

nodes:
  - name: gather-metrics
    tool: straylight-health
    args: ["report", "--format", "json"]
    output: metrics.json

  - name: analyze
    tool: straylight-alice
    args: ["forecast", "--horizon", "24h", "--format", "json"]
    output: forecast.json
    depends_on: [gather-metrics]

  - name: render
    command: "report-generator --metrics metrics.json --forecast forecast.json -o report.html"
    depends_on: [gather-metrics, analyze]

  - name: notify
    tool: straylight-notify
    args: ["send", "--title", "Daily Report", "--body", "Report generated", "--attach", "report.html"]
    depends_on: [render]

settings:
  parallel: 2
  retry_failed: true
  max_retries: 3
```

## EXAMPLES

Open the visual editor:

```
straylight-pipe edit
```

Run a pipeline with 4-way parallelism:

```
straylight-pipe run daily-report.yaml --parallel 4
```

Show a pipeline as ASCII art:

```
straylight-pipe show daily-report.yaml --format ascii
```

Retry the failed "render" node:

```
straylight-pipe retry run-20260315 --node render
```

Validate before deploying:

```
straylight-pipe validate daily-report.yaml
```

## INTEGRATION

- **straylight-cron**: Pipelines can be scheduled as cron jobs.
- **straylight-flux**: Pipe can embed flux queries as transformation nodes.
- **straylight-weave**: Complex service compositions use pipe as the orchestration engine.
- **straylight-notify**: Pipeline completion or failure triggers notifications.
- **straylight-timeline**: Pipeline runs are tracked in the activity timeline.

## SEE ALSO

straylight-flux(1), straylight-cron(1), straylight-weave(1), straylight-notify(1), straylight-timeline(1)
