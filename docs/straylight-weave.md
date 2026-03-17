# straylight-weave

## NAME

straylight-weave -- service composition engine that wires microservices into declarative graphs

## SYNOPSIS

```
straylight-weave [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-weave composes multiple services into a unified application by defining their relationships, communication patterns, and lifecycle dependencies in a declarative manifest. Instead of manually configuring service discovery, load balancing, and health checks for each service, weave handles all of this from a single description.

A weave manifest declares which services exist, how they communicate (HTTP, gRPC, Unix sockets, StrayLight bus), their startup order, health checks, and scaling policies. weave launches and manages the services, establishes the communication channels, and monitors the composed application as a unit.

weave is designed for the common pattern of running multiple cooperating services on a single machine or small cluster. It is lighter weight than full container orchestration platforms but provides the essential service composition primitives: dependency ordering, health-gated startup, restart policies, and environment injection from straylight-vault.

## COMMANDS

### `up`

Start all services in a composition.

```
straylight-weave up <manifest.toml> [--detach]
```

### `down`

Stop all services in a composition.

```
straylight-weave down <manifest-name>
```

### `status`

Show the status of all services.

```
straylight-weave status [<manifest-name>] [--json]
```

### `logs`

View logs from one or all services.

```
straylight-weave logs [<service-name>] [--follow] [--tail <n>]
```

### `restart`

Restart one or all services.

```
straylight-weave restart [<service-name>]
```

### `validate`

Check a manifest for errors.

```
straylight-weave validate <manifest.toml>
```

### `scale`

Scale a service within the composition.

```
straylight-weave scale <service-name> --replicas <n>
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--detach` | Run in background |
| `--json` | JSON output |
| `--verbose` | Debug logging |

## CONFIGURATION

Manifest format:

```toml
[composition]
name = "ml-pipeline"

[services.api]
command = "./api-server --port 8080"
health = "http://localhost:8080/health"
restart = "on-failure"
env = { MODEL_PATH = "/models/v2" }
secrets = ["api/key"]       # from straylight-vault

[services.worker]
command = "./worker"
depends_on = ["api"]
replicas = 4
resources = { cpu = 2, memory = "8G", gpu = 1 }

[services.cache]
command = "redis-server"
depends_on = []
resources = { memory = "2G" }

[connections]
api_to_worker = { from = "api", to = "worker", protocol = "grpc", port = 9090 }
api_to_cache = { from = "api", to = "cache", protocol = "tcp", port = 6379 }
```

## EXAMPLES

Start a composition:

```
straylight-weave up ml-pipeline.toml --detach
```

Check service status:

```
straylight-weave status ml-pipeline
```

Follow logs from the API service:

```
straylight-weave logs api --follow
```

Scale the worker service:

```
straylight-weave scale worker --replicas 8
```

Validate before deploying:

```
straylight-weave validate ml-pipeline.toml
```

Stop everything:

```
straylight-weave down ml-pipeline
```

## INTEGRATION

- **straylight-vault**: Secrets are injected into services from vault.
- **straylight-capsule**: Service resources map to capsule contracts.
- **straylight-pipe**: Complex data pipelines use weave for service lifecycle management.
- **straylight-health**: Composed application health is monitored as a unit.
- **straylight-swarm**: Multi-node compositions are deployed via swarm.
- **straylight-sandbox**: Services can be individually sandboxed.

## SEE ALSO

straylight-pipe(1), straylight-capsule(1), straylight-vault(1), straylight-swarm(1), straylight-sandbox(1)
