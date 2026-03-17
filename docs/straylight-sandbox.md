# straylight-sandbox

## NAME

straylight-sandbox -- container isolation with namespace, seccomp, and landlock enforcement

## SYNOPSIS

```
straylight-sandbox [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-sandbox provides fine-grained process isolation using Linux kernel security primitives: namespaces (PID, network, mount, user, IPC), seccomp-BPF syscall filtering, and Landlock filesystem access control. Unlike full container runtimes, sandbox is lightweight and designed for isolating individual applications or commands with minimal overhead.

Each sandbox profile defines exactly what the confined process can access: which directories it can read or write, which syscalls it can invoke, which network ports it can bind, and which devices it can use. Profiles can be strict (deny-by-default) or permissive (allow-by-default with specific denials).

straylight-sandbox ships with pre-built profiles for common applications (browsers, compilers, media players) and provides tools for generating profiles automatically by observing application behavior during a training run.

## COMMANDS

### `run`

Execute a command inside a sandbox.

```
straylight-sandbox run [--profile <name>] -- <command> [args...]
```

### `profile`

Manage sandbox profiles.

```
straylight-sandbox profile list
straylight-sandbox profile show <name>
straylight-sandbox profile create <name> [--from-pid <pid>]
straylight-sandbox profile edit <name>
```

### `train`

Generate a profile by observing application behavior.

```
straylight-sandbox train -- <command> [args...]
```

### `test`

Dry-run a command against a profile (reports violations without enforcing).

```
straylight-sandbox test --profile <name> -- <command> [args...]
```

### `list`

List running sandboxed processes.

```
straylight-sandbox list [--json]
```

### `inspect`

Show the effective permissions of a running sandbox.

```
straylight-sandbox inspect <pid>
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--profile <name>` | Sandbox profile to apply |
| `--net <none\|private\|host>` | Network namespace mode |
| `--fs <ro\|rw\|none>` | Default filesystem access |
| `--json` | JSON output |
| `--verbose` | Debug logging |

## CONFIGURATION

Profile format:

```toml
[sandbox]
name = "browser"
mode = "strict"

[filesystem]
read = ["/usr", "/lib", "/etc/ssl", "/etc/fonts", "~/.config/browser"]
write = ["~/.cache/browser", "/tmp"]
deny = ["/etc/shadow", "/etc/ssh"]

[syscalls]
allow = ["read", "write", "mmap", "futex", "epoll_wait", "..."]
deny = ["ptrace", "mount", "reboot"]

[network]
outbound = true
inbound = false
allowed_ports = [80, 443]

[devices]
gpu = true
audio = true
camera = false

[resources]
memory = "4G"
cpu_shares = 512
```

## EXAMPLES

Run a browser in a strict sandbox:

```
straylight-sandbox run --profile browser -- firefox
```

Generate a profile by training:

```
straylight-sandbox train -- ./my-app --test-mode
```

Test a profile without enforcing:

```
straylight-sandbox test --profile custom-app -- ./my-app
```

Run with no network access:

```
straylight-sandbox run --net none -- ./untrusted-script.sh
```

List active sandboxes:

```
straylight-sandbox list --json
```

## INTEGRATION

- **straylight-garden**: Development environments use sandbox for untrusted code.
- **straylight-capsule**: Sandbox profiles can include resource contracts.
- **straylight-shield**: Security audits check that sensitive applications are sandboxed.
- **straylight-vault**: Sandboxed processes access secrets through vault's sealed interface.
- **straylight-fuse**: Process fusion respects sandbox boundaries.

## SEE ALSO

straylight-shield(1), straylight-vault(1), straylight-capsule(1), straylight-garden(1), straylight-fuse(1)
