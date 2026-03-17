# straylight-migrate

## NAME

straylight-migrate -- full system migration between machines preserving state, secrets, and identity

## SYNOPSIS

```
straylight-migrate [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-migrate moves an entire StrayLight OS installation from one machine to another. It transfers the operating system, user data, installed packages, configuration, secrets (via straylight-vault), network identity, and system state. The result is a destination machine that behaves identically to the source, as though the hardware had simply been replaced.

Migration is performed in phases: inventory, transfer, verification, and cutover. During the transfer phase, data is streamed over an encrypted channel with delta compression to minimize bandwidth. The source system remains operational throughout -- cutover happens atomically at the end, and the source can be kept as a fallback.

straylight-migrate handles hardware differences gracefully. It adjusts driver configurations, regenerates initramfs for the destination hardware, and remaps device paths. It also integrates with straylight-vault to securely transfer secrets without exposing them during transit.

## COMMANDS

### `plan`

Analyze the source system and generate a migration plan.

```
straylight-migrate plan --to <destination> [--output <plan.toml>]
```

### `execute`

Run a migration plan.

```
straylight-migrate execute <plan.toml> [--dry-run]
```

### `status`

Check migration progress.

```
straylight-migrate status [<migration-id>]
```

### `verify`

Verify a completed migration.

```
straylight-migrate verify <migration-id>
```

### `rollback`

Revert a migration on the destination.

```
straylight-migrate rollback <migration-id>
```

### `resume`

Resume an interrupted migration.

```
straylight-migrate resume <migration-id>
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--to <destination>` | Target machine (hostname or IP) |
| `--exclude <paths>` | Comma-separated paths to exclude |
| `--bandwidth <limit>` | Transfer bandwidth limit |
| `--encrypt` | Encrypt data in transit (default: true) |
| `--json` | JSON output |
| `--verbose` | Debug logging |

## CONFIGURATION

```toml
[migrate]
default_transport = "ssh"
compress = true
encrypt = true
verify_checksums = true

[exclude]
paths = ["/tmp", "/var/cache", "/var/tmp"]
patterns = ["*.swap", "*.tmp"]

[hardware]
auto_adapt = true
regenerate_initramfs = true

[secrets]
use_vault = true
```

## EXAMPLES

Generate a migration plan:

```
straylight-migrate plan --to new-workstation.local
```

Execute with a bandwidth limit:

```
straylight-migrate execute migration-plan.toml --bandwidth 100M
```

Dry-run to see what would transfer:

```
straylight-migrate execute migration-plan.toml --dry-run
```

Check progress:

```
straylight-migrate status mig-20260315
```

Verify the migration completed correctly:

```
straylight-migrate verify mig-20260315
```

Resume after a network interruption:

```
straylight-migrate resume mig-20260315
```

## INTEGRATION

- **straylight-vault**: Secrets are transferred securely via vault's sealed transport.
- **straylight-snapshot**: A snapshot is taken before migration for safety.
- **straylight-mirror**: Mirror handles the low-level block transfer.
- **straylight-boot**: Boot configuration is adapted for the destination hardware.
- **straylight-echo**: Migration creates an echo checkpoint on both source and destination.
- **straylight-link**: Managed symlinks are recreated correctly on the destination.

## SEE ALSO

straylight-mirror(1), straylight-vault(1), straylight-snapshot(1), straylight-boot(1), straylight-ghost(1)
