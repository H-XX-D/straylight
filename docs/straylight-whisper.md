# straylight-whisper

## NAME

straylight-whisper -- encrypted IPC channel for inter-process secret exchange

## SYNOPSIS

```
straylight-whisper [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-whisper provides encrypted inter-process communication channels for processes that need to exchange sensitive data. While standard IPC mechanisms (pipes, Unix sockets, shared memory) transmit data in plaintext and are readable by root, whisper establishes end-to-end encrypted channels between processes using keys managed by straylight-vault.

Each whisper channel uses ephemeral Diffie-Hellman key exchange authenticated by process identity certificates. The resulting channel provides forward secrecy -- even if long-term keys are later compromised, past communications remain protected. Data is encrypted with AES-256-GCM and authenticated to prevent tampering.

whisper is transparent to applications that use it. A whisper channel presents the same file descriptor interface as a standard Unix socket, so existing code can adopt whisper by changing a single socket creation call.

## COMMANDS

### `create`

Create a named whisper channel.

```
straylight-whisper create <channel-name> --peers <pid1>,<pid2>
```

### `connect`

Connect to an existing whisper channel.

```
straylight-whisper connect <channel-name> [--fd <fd-number>]
```

### `list`

List active whisper channels.

```
straylight-whisper list [--json]
```

### `destroy`

Tear down a whisper channel.

```
straylight-whisper destroy <channel-name>
```

### `status`

Show channel status and encryption details.

```
straylight-whisper status <channel-name> [--verbose]
```

### `audit`

Show the access log for a channel.

```
straylight-whisper audit <channel-name> [--since <datetime>]
```

### `proxy`

Create a whisper-encrypted proxy for a standard socket.

```
straylight-whisper proxy --listen <path> --target <path>
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--cipher <algorithm>` | Encryption algorithm (default: AES-256-GCM) |
| `--json` | JSON output |
| `--verbose` | Debug logging |

## CONFIGURATION

```toml
[whisper]
socket_dir = "/run/straylight/whisper"
key_backend = "vault"

[crypto]
cipher = "AES-256-GCM"
kdf = "HKDF-SHA256"
key_rotation_interval = "1h"

[access]
require_process_attestation = true
audit_all = true
```

## EXAMPLES

Create a channel between two processes:

```
straylight-whisper create db-channel --peers 4821,4822
```

Wrap an existing socket with encryption:

```
straylight-whisper proxy --listen /tmp/app.sock --target /run/db.sock
```

List active channels:

```
straylight-whisper list --json
```

Check encryption details:

```
straylight-whisper status db-channel --verbose
```

Audit channel access:

```
straylight-whisper audit db-channel --since "24h ago"
```

## INTEGRATION

- **straylight-vault**: Encryption keys are derived from vault-managed secrets.
- **straylight-sandbox**: Sandboxed processes use whisper for secure communication.
- **straylight-fuse**: Fused processes can use whisper for their internal channels.
- **straylight-ghost**: Whisper channels are re-established after process migration.
- **straylight-shield**: Security audit verifies that sensitive IPC uses whisper.

## SEE ALSO

straylight-vault(1), straylight-sandbox(1), straylight-fuse(1), straylight-ghost(1), straylight-shield(1)
