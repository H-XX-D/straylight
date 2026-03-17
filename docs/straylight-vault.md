# straylight-vault

## NAME

straylight-vault -- secret storage backed by the TPM and hardware security modules

## SYNOPSIS

```
straylight-vault [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-vault provides secure storage for secrets: API keys, passwords, certificates, encryption keys, and application credentials. Secrets are encrypted at rest using keys derived from the TPM (Trusted Platform Module), ensuring they cannot be extracted even if the disk is stolen. On systems without a TPM, vault falls back to passphrase-derived encryption.

vault exposes secrets to authorized applications through a socket-based API that delivers secrets directly into process memory without writing them to disk or environment variables. Access control is enforced per-secret, with policies defining which users and applications can read, write, or rotate each secret.

The tool supports secret rotation, expiration, and auditing. Every secret access is logged to straylight-timeline, and vault integrates with straylight-shield for compliance checking.

## COMMANDS

### `set`

Store a secret.

```
straylight-vault set <key> [--value <value>] [--file <path>] [--expires <duration>]
```

### `get`

Retrieve a secret.

```
straylight-vault get <key> [--format <raw|json>]
```

### `delete`

Remove a secret.

```
straylight-vault delete <key>
```

### `list`

List stored secrets (names only, not values).

```
straylight-vault list [--json] [--expired]
```

### `rotate`

Rotate a secret's value.

```
straylight-vault rotate <key> [--generator <type>]
```

### `policy`

Manage access policies.

```
straylight-vault policy set <key> --allow <user|app> --permissions <read|write|rotate>
straylight-vault policy show <key>
```

### `export`

Export secrets as an encrypted archive.

```
straylight-vault export --output <path> --passphrase <passphrase> [--keys <list>]
```

### `import`

Import secrets from an encrypted archive.

```
straylight-vault import <path> --passphrase <passphrase>
```

### `seal` / `unseal`

Lock or unlock the vault (requires TPM or passphrase).

```
straylight-vault seal
straylight-vault unseal [--passphrase <passphrase>]
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--json` | JSON output |
| `--verbose` | Debug logging |

## CONFIGURATION

```toml
[vault]
store_path = "/var/lib/straylight/vault"
backend = "tpm"             # tpm | passphrase | hsm

[tpm]
device = "/dev/tpmrm0"
pcr_policy = [0, 1, 7]

[access]
default_policy = "deny"
audit_all_access = true

[rotation]
default_expiry = "90d"
auto_rotate = true
```

## EXAMPLES

Store an API key:

```
straylight-vault set api/openai --value "sk-..." --expires 90d
```

Retrieve a secret:

```
straylight-vault get api/openai
```

Set an access policy:

```
straylight-vault policy set api/openai --allow app:inference-service --permissions read
```

Rotate a secret:

```
straylight-vault rotate db/password --generator alphanumeric
```

Export for migration:

```
straylight-vault export --output secrets.vault --passphrase "migration-key" --keys "api/*"
```

List expired secrets:

```
straylight-vault list --expired
```

## INTEGRATION

- **straylight-sandbox**: Sandboxed processes access secrets through vault's sealed API.
- **straylight-remote**: Machine certificates are managed by vault.
- **straylight-migrate**: Secrets are transferred securely during system migration.
- **straylight-whisper**: Encrypted IPC uses vault-managed keys.
- **straylight-shield**: Audit checks that sensitive data is stored in vault.
- **straylight-timeline**: All secret accesses are logged.

## SEE ALSO

straylight-shield(1), straylight-whisper(1), straylight-remote(1), straylight-migrate(1), straylight-sandbox(1)
