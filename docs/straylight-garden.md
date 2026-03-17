# straylight-garden

## NAME

straylight-garden -- environment manager for reproducible development and build environments

## SYNOPSIS

```
straylight-garden [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-garden creates and manages isolated, reproducible environments for software development, building, and testing. Each environment (called a "plot") is a self-contained namespace with its own package set, environment variables, mount points, and toolchains. Plots are defined declaratively in a TOML manifest and can be version-controlled alongside project source code.

Unlike containers, garden environments share the host kernel and can selectively expose host resources (GPUs, audio devices, network interfaces) without complex passthrough configuration. This makes them ideal for development workflows where full isolation is unnecessary but reproducibility is essential.

garden integrates with straylight-sandbox for security isolation when needed, and with straylight-capsule for resource guarantees. Environments can be stacked -- a base plot providing system libraries can be overlaid with project-specific toolchains.

## COMMANDS

### `create`

Create a new environment from a manifest.

```
straylight-garden create <name> [--from <manifest.toml>] [--base <plot>]
```

### `enter`

Enter an environment (spawns a shell).

```
straylight-garden enter <name> [-- <command>]
```

### `destroy`

Remove an environment and its state.

```
straylight-garden destroy <name> [--keep-cache]
```

### `list`

List all environments.

```
straylight-garden list [--json]
```

### `export`

Export an environment as a portable archive.

```
straylight-garden export <name> --output <path>
```

### `import`

Import an environment from an archive.

```
straylight-garden import <archive> [--name <name>]
```

### `update`

Update packages in an environment.

```
straylight-garden update <name> [--dry-run]
```

### `diff`

Show differences between two environments.

```
straylight-garden diff <name-a> <name-b>
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--json` | JSON output |
| `--verbose` | Debug logging |
| `--no-cache` | Do not use cached packages |

## CONFIGURATION

Plot manifest format:

```toml
[plot]
name = "rust-dev"
base = "straylight-base"

[packages]
install = ["rustc", "cargo", "lldb", "mold"]
channels = ["stable"]

[env]
RUST_BACKTRACE = "1"
CARGO_HOME = "/plot/cargo"

[mounts]
"/home/user/projects" = { host = "/home/user/projects", writable = true }

[devices]
gpu = true
audio = false

[resources]
memory = "8G"
cpu_cores = 4
```

## EXAMPLES

Create a Rust development environment:

```
straylight-garden create rust-dev --from rust-dev.toml
```

Enter an environment and run a build:

```
straylight-garden enter rust-dev -- cargo build --release
```

Stack a CUDA overlay on top of a base environment:

```
straylight-garden create ml-env --base rust-dev --from cuda-overlay.toml
```

Export for a teammate:

```
straylight-garden export rust-dev --output rust-dev.garden.tar.zst
```

Compare two environments:

```
straylight-garden diff rust-dev ml-env
```

## INTEGRATION

- **straylight-sandbox**: Gardens can be combined with sandbox profiles for untrusted code.
- **straylight-capsule**: Environments can declare resource contracts.
- **straylight-link**: Manages symlinks between plots and host paths.
- **straylight-update**: Gardens track their own package versions independently.
- **straylight-mirror**: Full environments can be cloned to another machine.

## SEE ALSO

straylight-sandbox(1), straylight-capsule(1), straylight-link(1), straylight-mirror(1), straylight-update(1)
