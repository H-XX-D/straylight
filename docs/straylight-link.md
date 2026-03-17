# straylight-link

## NAME

straylight-link -- symlink manager that tracks, validates, and repairs symbolic links

## SYNOPSIS

```
straylight-link [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-link manages symbolic links across the StrayLight filesystem. It maintains a registry of all managed symlinks, detects broken links, resolves conflicts, and can repair or recreate links when targets move. This is particularly useful for managing configuration overlays, dotfiles, and environment-specific paths.

Unlike raw `ln -s`, straylight-link tracks the intent behind each link -- why it was created, which tool or profile owns it, and what should happen if the target disappears. Links can be grouped into profiles (e.g., "development", "production") and activated or deactivated as a set.

link also watches for filesystem changes that would break managed links and can automatically repair them, log warnings, or notify the user depending on policy.

## COMMANDS

### `create`

Create a managed symlink.

```
straylight-link create <target> <link-path> [--profile <name>] [--tag <label>]
```

### `remove`

Remove a managed symlink.

```
straylight-link remove <link-path>
```

### `check`

Validate all managed links.

```
straylight-link check [--fix] [--profile <name>]
```

### `list`

List managed symlinks.

```
straylight-link list [--profile <name>] [--broken] [--json]
```

### `profile`

Manage link profiles.

```
straylight-link profile create <name>
straylight-link profile activate <name>
straylight-link profile deactivate <name>
straylight-link profile list
```

### `import`

Import existing symlinks into management.

```
straylight-link import <directory> [--recursive]
```

### `watch`

Monitor managed links for breakage.

```
straylight-link watch [--auto-fix]
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--json` | JSON output |
| `--force` | Overwrite existing files |
| `--verbose` | Debug logging |

## CONFIGURATION

```toml
[link]
registry_path = "/var/lib/straylight/link/registry.db"
watch = true
auto_fix = false

[profiles]
default = "system"

[notifications]
on_broken = true
on_fix = true
```

## EXAMPLES

Create a managed symlink:

```
straylight-link create /etc/nginx/nginx.conf ~/.dotfiles/nginx.conf --profile web
```

Check for broken links and fix them:

```
straylight-link check --fix
```

List all broken links:

```
straylight-link list --broken
```

Activate a profile:

```
straylight-link profile activate development
```

Import existing dotfile symlinks:

```
straylight-link import ~/.config --recursive
```

## INTEGRATION

- **straylight-garden**: Environment managers creates symlinks through link for tracking.
- **straylight-echo**: Link changes are registered for system-wide undo.
- **straylight-snapshot**: Snapshots preserve link state.
- **straylight-migrate**: Migration preserves and recreates managed links.
- **straylight-health**: Broken links contribute to the filesystem health score.

## SEE ALSO

straylight-garden(1), straylight-echo(1), straylight-snapshot(1), straylight-migrate(1)
