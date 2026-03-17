# straylight-probe

## NAME

straylight-probe -- network scanner and diagnostic toolkit

## SYNOPSIS

```
straylight-probe [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-probe is a comprehensive network diagnostic tool that combines the functionality of ping, traceroute, port scanning, DNS lookup, and bandwidth testing into a single coherent interface. It is designed for both quick troubleshooting and deep network analysis.

probe can discover hosts on the local network, test connectivity to remote services, measure latency and jitter over time, identify MTU issues, and analyze DNS resolution paths. All results are structured and can be output as JSON for consumption by other tools or dashboards.

The tool operates with awareness of StrayLight's network configuration, automatically using the correct source interface and respecting VPN routing rules managed by straylight-network.

## COMMANDS

### `ping`

Test connectivity with enhanced statistics.

```
straylight-probe ping <host> [--count <n>] [--interval <duration>]
```

### `trace`

Trace the network path to a host.

```
straylight-probe trace <host> [--protocol <icmp|tcp|udp>] [--port <port>]
```

### `scan`

Scan ports on a host.

```
straylight-probe scan <host> [--ports <range>] [--protocol <tcp|udp>]
```

### `discover`

Discover hosts on the local network.

```
straylight-probe discover [--subnet <cidr>] [--timeout <duration>]
```

### `dns`

Perform DNS lookups with diagnostic information.

```
straylight-probe dns <name> [--type <A|AAAA|MX|NS|TXT>] [--server <resolver>]
```

### `bandwidth`

Measure bandwidth to a host.

```
straylight-probe bandwidth <host> [--duration <seconds>] [--direction <up|down|both>]
```

### `mtu`

Discover the path MTU to a host.

```
straylight-probe mtu <host>
```

### `monitor`

Continuously monitor connectivity to a set of hosts.

```
straylight-probe monitor <hosts...> [--interval <duration>] [--alert-on <loss|latency>]
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--interface <name>` | Source network interface |
| `--json` | JSON output |
| `--timeout <duration>` | Per-probe timeout |
| `--verbose` | Debug logging |

## CONFIGURATION

```toml
[probe]
default_timeout = "5s"
default_interface = "auto"

[scan]
rate_limit = 1000           # packets per second
stealth = false

[monitor]
default_interval = "10s"
alert_latency_ms = 100
alert_loss_pct = 5

[dns]
default_servers = ["system"]
```

## EXAMPLES

Ping with detailed statistics:

```
straylight-probe ping 8.8.8.8 --count 100
```

TCP traceroute to a web server:

```
straylight-probe trace example.com --protocol tcp --port 443
```

Scan common ports:

```
straylight-probe scan 192.168.1.1 --ports 1-1024
```

Discover devices on the local network:

```
straylight-probe discover --subnet 192.168.1.0/24
```

Test DNS resolution path:

```
straylight-probe dns example.com --type A --verbose
```

Monitor multiple hosts continuously:

```
straylight-probe monitor gateway.local dns.local cloud-api.example.com --interval 5s
```

## INTEGRATION

- **straylight-network**: Probe uses network's interface and routing configuration.
- **straylight-health**: Network health checks use probe for connectivity testing.
- **straylight-alice**: Latency anomalies detected by probe feed into the AI monitor.
- **straylight-notify**: Alert conditions trigger notifications.
- **straylight-timeline**: Probe results are logged to the activity timeline.

## SEE ALSO

straylight-network(1), straylight-health(1), straylight-alice(1), straylight-remote(1), straylight-swarm(1)
