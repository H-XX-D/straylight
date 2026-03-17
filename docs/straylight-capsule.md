# straylight-capsule

## NAME

straylight-capsule -- resource-contract packages that declare and enforce CPU, memory, and I/O guarantees

## SYNOPSIS

```
straylight-capsule [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-capsule introduces a declarative resource-contract model to StrayLight OS. A capsule is a package manifest that specifies the minimum and maximum CPU shares, memory, disk I/O bandwidth, and network bandwidth that an application or service requires. When a capsule is activated, the system provisions the requested resources via cgroups v2 and guarantees they remain available for the duration of the workload.

Capsules solve the problem of resource contention in mixed-workload environments. A database server can declare that it needs at least 4 GiB of memory and 50% of disk I/O bandwidth, and straylight-capsule will enforce that contract even when other applications attempt to consume more than their share. If the system lacks capacity to honor a new capsule, activation is denied with a clear explanation of the conflict.

Capsule manifests are TOML files that live alongside application configurations. They can be versioned, shared, and composed. straylight-capsule integrates with straylight-quota for budget enforcement and straylight-sandbox for isolation.

## COMMANDS

### `activate`

Activate a capsule and provision its resources.

```
straylight-capsule activate <capsule.toml> [--force]
```

### `deactivate`

Release resources held by a capsule.

```
straylight-capsule deactivate <capsule-name>
```

### `list`

List all active capsules and their resource usage.

```
straylight-capsule list [--json] [--all]
```

### `inspect`

Show detailed resource allocation for a capsule.

```
straylight-capsule inspect <capsule-name>
```

### `validate`

Check a capsule manifest for correctness and feasibility.

```
straylight-capsule validate <capsule.toml>
```

### `create`

Generate a capsule manifest interactively or from a running process.

```
straylight-capsule create [--from-pid <pid>] [--output <path>]
```

### `adjust`

Modify resource limits of an active capsule.

```
straylight-capsule adjust <capsule-name> --memory <value> [--cpu <shares>]
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--force` | Activate even if contracts conflict (best-effort mode) |
| `--json` | JSON output |
| `--dry-run` | Validate and report without activating |
| `--verbose` | Debug logging |

## CONFIGURATION

Capsule manifest format:

```toml
[capsule]
name = "postgres-primary"
version = "1.0"

[resources.cpu]
shares = 1024
min_cores = 2
max_cores = 8

[resources.memory]
min = "4G"
max = "16G"
swap = "0"

[resources.io]
read_bps = "500M"
write_bps = "200M"
iops = 10000

[resources.network]
bandwidth = "1G"
priority = "high"

[lifecycle]
on_exceed = "throttle"       # throttle | warn | kill
on_underuse_reclaim = true
grace_period = "30s"
```

System configuration in `/etc/straylight/capsule.toml`:

```toml
[enforcement]
strict = true
overcommit_ratio = 1.2

[defaults]
memory_max = "4G"
cpu_shares = 512
```

## EXAMPLES

Activate a capsule for a database workload:

```
straylight-capsule activate /etc/straylight/capsules/postgres.toml
```

Generate a capsule from a running process:

```
straylight-capsule create --from-pid 4821 --output my-app.toml
```

List all active capsules in JSON:

```
straylight-capsule list --json
```

Adjust memory limit for an active capsule:

```
straylight-capsule adjust postgres-primary --memory 32G
```

Validate a capsule before deployment:

```
straylight-capsule validate my-app.toml
```

## INTEGRATION

- **straylight-quota**: Capsule resource requests are validated against user/group quotas.
- **straylight-sandbox**: Capsules can be combined with sandbox profiles for full isolation.
- **straylight-autotune**: Autotune respects capsule contracts when adjusting global parameters.
- **straylight-swarm**: Capsules are the unit of deployment in multi-node orchestration.
- **straylight-health**: Capsule health is monitored and reported.

## SEE ALSO

straylight-quota(1), straylight-sandbox(1), straylight-autotune(1), straylight-swarm(1), straylight-fuse(1)
