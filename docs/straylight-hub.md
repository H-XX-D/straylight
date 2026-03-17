# straylight-hub

## NAME

straylight-hub -- central dashboard aggregating status from every StrayLight subsystem

## SYNOPSIS

```
straylight-hub [OPTIONS] [COMMAND]
```

## DESCRIPTION

straylight-hub is the graphical central dashboard for StrayLight OS. It provides a single-pane view of the entire system: health scores, resource utilization, active alerts, running services, network topology, and storage status. The hub runs as a local web application accessible at `http://localhost:9480` or as a native Wayland window.

The dashboard is organized into tiles that can be rearranged, resized, and configured. Each tile connects to a StrayLight tool's bus endpoint and displays its data in real time. Users can create custom dashboards for different roles (developer, sysadmin, data scientist) and switch between them.

hub also serves as a launching point for deeper investigation. Clicking an alert tile opens straylight-alice's explanation. Clicking a process tile opens straylight-trace. Clicking a disk tile opens straylight-disk's detail view. This contextual navigation makes hub the natural starting point for any system management task.

## COMMANDS

### `start`

Start the hub server.

```
straylight-hub start [--port <port>] [--mode <web|native>]
```

### `stop`

Stop the hub server.

```
straylight-hub stop
```

### `status`

Check if the hub is running.

```
straylight-hub status
```

### `dashboard`

Manage dashboard layouts.

```
straylight-hub dashboard list
straylight-hub dashboard create <name> --from <template>
straylight-hub dashboard export <name> --output <path>
straylight-hub dashboard import <path>
```

### `open`

Open the hub in the default browser or native window.

```
straylight-hub open [--dashboard <name>]
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--port <port>` | HTTP port (default: 9480) |
| `--mode <web\|native>` | Display mode |
| `--bind <address>` | Bind address (default: 127.0.0.1) |
| `--no-auth` | Disable authentication (local only) |

## CONFIGURATION

```toml
[hub]
port = 9480
bind = "127.0.0.1"
mode = "web"
default_dashboard = "overview"
auto_start = true

[auth]
enabled = true
method = "pam"

[tiles]
refresh_interval = "1s"
animation = true
theme = "straylight-dark"

[dashboards]
store_path = "~/.config/straylight/hub/dashboards"
```

## EXAMPLES

Start the hub and open it:

```
straylight-hub start && straylight-hub open
```

Start in native window mode:

```
straylight-hub start --mode native
```

Create a GPU-focused dashboard:

```
straylight-hub dashboard create gpu-monitor --from gpu-template
```

Export a dashboard for sharing:

```
straylight-hub dashboard export my-dashboard --output my-dashboard.json
```

Access the hub remotely (on a trusted network):

```
straylight-hub start --bind 0.0.0.0 --port 9480
```

## INTEGRATION

- **straylight-dash**: Hub is the graphical counterpart to dash's terminal interface.
- **straylight-alice**: Alert tiles display AI-generated anomaly explanations.
- **straylight-health**: Health score is displayed as the primary status indicator.
- **straylight-trace**: Click-through from process tiles to the application tracer.
- **straylight-fabric**: Device topology is rendered as an interactive graph.
- **straylight-thermal**: Thermal map tile shows zone temperatures.

## SEE ALSO

straylight-dash(1), straylight-health(1), straylight-alice(1), straylight-fabric(1), straylight-notify(1)
