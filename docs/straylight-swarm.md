# straylight-swarm

## NAME

straylight-swarm -- multi-node orchestration for workload placement and cluster health

## SYNOPSIS

```
straylight-swarm [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-swarm manages clusters of StrayLight machines as a unified compute surface. It handles node discovery, workload scheduling, health monitoring, and automatic failover. Swarm enables applications to run across multiple machines without being aware of the distributed infrastructure.

Swarm uses a leader-elected consensus protocol for cluster state. Each node runs a swarm agent that reports its resources, health, and active workloads. The scheduler places new workloads based on resource availability, topology affinity, capsule contracts, and custom constraints. When a node fails, swarm automatically reschedules its workloads to healthy nodes using straylight-ghost for live process migration.

The tool supports both imperative (run this here) and declarative (ensure N replicas are running) deployment models. Declarative deployments are continuously reconciled -- if a node goes down and the replica count drops, swarm restarts the workload elsewhere.

## COMMANDS

### `init`

Initialize a new cluster.

```
straylight-swarm init [--name <cluster-name>]
```

### `join`

Join an existing cluster.

```
straylight-swarm join <leader-address> [--token <token>]
```

### `leave`

Leave the cluster.

```
straylight-swarm leave [--drain]
```

### `nodes`

List cluster nodes.

```
straylight-swarm nodes [--json] [--verbose]
```

### `deploy`

Deploy a workload.

```
straylight-swarm deploy <manifest.toml> [--replicas <n>]
```

### `scale`

Scale a deployment.

```
straylight-swarm scale <deployment-name> --replicas <n>
```

### `status`

Show cluster and deployment status.

```
straylight-swarm status [<deployment-name>] [--json]
```

### `drain`

Drain a node (migrate all workloads off).

```
straylight-swarm drain <node-name>
```

### `cordon` / `uncordon`

Mark a node as unschedulable / schedulable.

```
straylight-swarm cordon <node-name>
straylight-swarm uncordon <node-name>
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--json` | JSON output |
| `--verbose` | Debug logging |

## CONFIGURATION

```toml
[swarm]
cluster_name = "straylight-cluster"
discovery = "multicast"
port = 9495
data_dir = "/var/lib/straylight/swarm"

[scheduling]
strategy = "bin-pack"       # bin-pack | spread | topology-aware
preemption = false

[failover]
detection_timeout = "30s"
auto_reschedule = true

[tls]
enabled = true
ca = "/etc/straylight/certs/ca.pem"
```

Deployment manifest:

```toml
[deployment]
name = "inference-service"
replicas = 3

[resources]
cpu = 4
memory = "16G"
gpu = 1

[constraints]
node_labels = ["gpu=a100"]
spread = "zone"

[health]
check = "http://localhost:8080/health"
interval = "10s"
```

## EXAMPLES

Initialize a cluster:

```
straylight-swarm init --name production
```

Join a cluster:

```
straylight-swarm join 192.168.1.10:9495 --token abc123
```

Deploy with 3 replicas:

```
straylight-swarm deploy inference-service.toml --replicas 3
```

Scale up:

```
straylight-swarm scale inference-service --replicas 5
```

Drain a node for maintenance:

```
straylight-swarm drain node-03
```

View cluster status:

```
straylight-swarm status --json
```

## INTEGRATION

- **straylight-ghost**: Process migration during drain and failover.
- **straylight-mesh**: GPU pool is managed cluster-wide through swarm.
- **straylight-capsule**: Deployment manifests reference capsule resource contracts.
- **straylight-health**: Node health determines scheduling eligibility.
- **straylight-remote**: Cluster communication uses remote's authenticated channel.
- **straylight-bridge**: Cross-node shared memory for distributed workloads.

## SEE ALSO

straylight-ghost(1), straylight-mesh(1), straylight-capsule(1), straylight-remote(1), straylight-health(1)
