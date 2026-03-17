# straylight-policy

## NAME

straylight-policy -- system role and policy engine for declarative machine configuration

## SYNOPSIS

```
straylight-policy [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-policy enforces declarative configuration policies on StrayLight OS. A policy is a set of rules that define the desired state of the system: which services should be running, what kernel parameters should be set, which packages must be installed, and what security settings are required. Policies are expressed in TOML and can be composed, inherited, and overridden.

The policy engine continuously reconciles the actual system state against the declared policy. When a drift is detected -- a service was stopped, a configuration file was modified, a package was removed -- the engine can alert, auto-remediate, or block the change depending on the enforcement level.

Policies can be scoped to machine roles (workstation, server, kiosk, build-node), user groups, or individual machines. They integrate with straylight-users for role-based access control and with straylight-shield for security policy enforcement.

## COMMANDS

### `apply`

Apply a policy to the system.

```
straylight-policy apply <policy.toml> [--dry-run]
```

### `check`

Check current system compliance against a policy.

```
straylight-policy check [<policy.toml>] [--json]
```

### `diff`

Show differences between current state and policy.

```
straylight-policy diff <policy.toml>
```

### `list`

List active policies.

```
straylight-policy list [--json]
```

### `create`

Generate a policy from the current system state.

```
straylight-policy create --output <policy.toml> [--scope <full|services|security>]
```

### `enforce`

Start the continuous enforcement daemon.

```
straylight-policy enforce [--mode <alert|remediate|block>]
```

### `audit`

Generate a compliance audit report.

```
straylight-policy audit [--format <text|json|html>]
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--dry-run` | Show changes without applying |
| `--mode <mode>` | Enforcement mode: alert, remediate, block |
| `--json` | JSON output |
| `--verbose` | Debug logging |

## CONFIGURATION

Policy file format:

```toml
[policy]
name = "workstation-standard"
role = "workstation"
inherits = ["base-security"]

[services]
required = ["straylight-health", "straylight-alice", "sshd"]
forbidden = ["telnetd"]

[packages]
required = ["straylight-tools", "firmware-linux"]
forbidden = ["netcat-traditional"]

[kernel]
"vm.swappiness" = 10
"net.ipv4.ip_forward" = 0

[security]
firewall = true
selinux = "enforcing"
password_min_length = 12

[filesystem]
"/etc/shadow" = { mode = "0640", owner = "root", group = "shadow" }

[enforcement]
mode = "remediate"
check_interval = "5m"
```

## EXAMPLES

Apply a workstation policy:

```
straylight-policy apply workstation-standard.toml
```

Check compliance:

```
straylight-policy check workstation-standard.toml
```

Generate a policy from the current system:

```
straylight-policy create --output current-state.toml
```

Start continuous enforcement:

```
straylight-policy enforce --mode remediate
```

Generate an audit report:

```
straylight-policy audit --format html > compliance-report.html
```

## INTEGRATION

- **straylight-users**: Policies can define role-based access rules.
- **straylight-shield**: Security policies are validated against shield's audit framework.
- **straylight-autotune**: Kernel parameters in policies take precedence over autotune.
- **straylight-echo**: Policy changes are registered for undo.
- **straylight-update**: Updates are checked against policy constraints.

## SEE ALSO

straylight-users(1), straylight-shield(1), straylight-autotune(1), straylight-echo(1), straylight-update(1)
