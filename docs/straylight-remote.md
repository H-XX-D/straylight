# straylight-remote

## NAME

straylight-remote -- remote system control with an SSH-free authenticated channel

## SYNOPSIS

```
straylight-remote [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-remote provides secure, authenticated remote access to StrayLight machines without relying on SSH. It uses mutual TLS authentication with certificates managed by straylight-vault, offering a simpler trust model than SSH key distribution. Remote sessions support full terminal access, file transfer, and direct tool invocation.

The tool is designed for managing StrayLight clusters where traditional SSH key management becomes unwieldy. Each machine has a machine certificate issued by the cluster CA, and user certificates are tied to straylight-users identities. This provides fine-grained access control -- a user can be authorized to run specific tools on specific machines without shell access.

remote supports session multiplexing, connection persistence, and automatic reconnection. It also provides an API mode for programmatic remote tool invocation from scripts and automation systems.

## COMMANDS

### `connect`

Open a remote terminal session.

```
straylight-remote connect <host> [--user <name>]
```

### `exec`

Execute a command on a remote host.

```
straylight-remote exec <host> -- <command> [args...]
```

### `copy`

Transfer files to or from a remote host.

```
straylight-remote copy <source> <destination>
```

Source/destination format: `[host:]path`

### `tunnel`

Create a port tunnel.

```
straylight-remote tunnel <host> --local <port> --remote <port>
```

### `hosts`

List known hosts and their status.

```
straylight-remote hosts [--json]
```

### `authorize`

Grant a user access to a remote host.

```
straylight-remote authorize <user> --host <host> [--tools <list>]
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--user <name>` | Remote user identity |
| `--cert <path>` | Client certificate |
| `--json` | JSON output |
| `--verbose` | Debug logging |

## CONFIGURATION

```toml
[remote]
port = 9492
ca_cert = "/etc/straylight/certs/ca.pem"
client_cert = "/etc/straylight/certs/machine.pem"
client_key = "/etc/straylight/certs/machine.key"

[sessions]
timeout = "30m"
keepalive = "30s"
multiplex = true

[access]
default_policy = "deny"
allowed_tools = ["straylight-health", "straylight-log"]
```

## EXAMPLES

Connect to a remote machine:

```
straylight-remote connect node-02
```

Run a health check on a remote node:

```
straylight-remote exec node-02 -- straylight-health status
```

Copy a file from a remote host:

```
straylight-remote copy node-02:/var/log/straylight/health.log ./health.log
```

Create a tunnel to a remote hub instance:

```
straylight-remote tunnel node-02 --local 9480 --remote 9480
```

Grant a user tool-specific access:

```
straylight-remote authorize alice --host node-02 --tools "straylight-health,straylight-log"
```

## INTEGRATION

- **straylight-vault**: Certificate management and secret distribution.
- **straylight-users**: User identity and authorization.
- **straylight-swarm**: Remote is the transport layer for cluster management.
- **straylight-ghost**: Process migration uses remote's authenticated channel.
- **straylight-shield**: Remote access is audited by the security framework.

## SEE ALSO

straylight-vault(1), straylight-users(1), straylight-swarm(1), straylight-shield(1), straylight-network(1)
