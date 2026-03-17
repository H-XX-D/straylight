# straylight-shield

## NAME

straylight-shield -- security audit framework scanning for CVEs, misconfigurations, and policy violations

## SYNOPSIS

```
straylight-shield [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-shield performs comprehensive security audits of StrayLight OS. It scans for known vulnerabilities (CVEs) in installed packages, checks for common misconfigurations (world-readable private keys, open ports, weak passwords), and validates compliance with security policies defined by straylight-policy.

shield maintains a local vulnerability database that is updated from the StrayLight security feed. Scans produce detailed reports with severity ratings, remediation steps, and references to relevant advisories. When straylight-hotpatch is available, shield can offer to apply fixes for detected vulnerabilities immediately.

The tool supports scheduled scanning via straylight-cron and real-time monitoring mode, where it watches for changes that introduce new vulnerabilities (e.g., a new package installation or a configuration file modification).

## COMMANDS

### `scan`

Run a security scan.

```
straylight-shield scan [--scope <full|packages|config|network|permissions>] [--json]
```

### `audit`

Generate a security audit report.

```
straylight-shield audit [--format <text|json|html>] [--output <path>]
```

### `check`

Check a specific CVE or misconfiguration.

```
straylight-shield check <cve-id|check-name>
```

### `fix`

Apply recommended fixes for detected issues.

```
straylight-shield fix <finding-id> [--dry-run]
```

### `watch`

Monitor for new security issues in real time.

```
straylight-shield watch [--alert-level <info|warning|critical>]
```

### `update`

Update the vulnerability database.

```
straylight-shield update
```

### `baseline`

Create or compare against a security baseline.

```
straylight-shield baseline create --output <path>
straylight-shield baseline check <baseline.toml>
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--scope <scope>` | Scan scope |
| `--severity <min>` | Minimum severity to report (low, medium, high, critical) |
| `--json` | JSON output |
| `--verbose` | Debug logging |

## CONFIGURATION

```toml
[shield]
db_path = "/var/lib/straylight/shield/vuln.db"
auto_update = true
update_interval = "6h"

[scan]
default_scope = "full"
schedule = "0 2 * * *"      # Daily at 2 AM
exclude_packages = []

[alerts]
min_severity = "medium"
delivery = ["bus", "notify"]

[compliance]
frameworks = ["cis-level1"]
```

## EXAMPLES

Full system security scan:

```
straylight-shield scan --scope full
```

Generate an HTML audit report:

```
straylight-shield audit --format html --output security-report.html
```

Check if a specific CVE affects this system:

```
straylight-shield check CVE-2026-1234
```

Apply a fix with preview:

```
straylight-shield fix finding-4821 --dry-run
```

Monitor for critical issues:

```
straylight-shield watch --alert-level critical
```

Update the vulnerability database:

```
straylight-shield update
```

## INTEGRATION

- **straylight-hotpatch**: Shield can trigger live patches for detected vulnerabilities.
- **straylight-policy**: Security findings are checked against policy compliance.
- **straylight-vault**: Scans verify that secrets are properly stored in vault.
- **straylight-sandbox**: Audits check that risky applications are sandboxed.
- **straylight-health**: Security score is a component of overall health.
- **straylight-notify**: Critical findings trigger immediate notifications.

## SEE ALSO

straylight-hotpatch(1), straylight-policy(1), straylight-vault(1), straylight-sandbox(1), straylight-health(1)
