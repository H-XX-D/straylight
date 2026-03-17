# straylight-users

## NAME

straylight-users -- user and group management with role-based access control

## SYNOPSIS

```
straylight-users [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-users manages user accounts, groups, and role-based access control on StrayLight OS. It goes beyond traditional Unix user management by integrating with StrayLight's quota system, vault-based credential storage, policy engine, and session management.

Each user has a profile that defines not only their identity and group memberships but also their resource quotas, default capsule, sandbox policy, and permitted tools. Users can be assigned roles (admin, developer, operator, guest) that automatically apply the appropriate policy and quota settings.

straylight-users supports local accounts, LDAP/Active Directory integration, and SSH key management. It also manages user sessions, providing visibility into who is logged in, what they are running, and how many resources they are consuming.

## COMMANDS

### `add`

Create a new user.

```
straylight-users add <username> [--role <role>] [--groups <list>] [--shell <path>]
```

### `remove`

Remove a user.

```
straylight-users remove <username> [--keep-home]
```

### `modify`

Modify a user's properties.

```
straylight-users modify <username> [--role <role>] [--groups <list>] [--shell <path>]
```

### `list`

List users.

```
straylight-users list [--json] [--role <role>]
```

### `info`

Show detailed user information.

```
straylight-users info <username> [--json]
```

### `session`

Manage user sessions.

```
straylight-users session list [--json]
straylight-users session kill <session-id>
```

### `role`

Manage roles.

```
straylight-users role list
straylight-users role create <name> --permissions <list>
straylight-users role show <name>
```

### `group`

Manage groups.

```
straylight-users group list
straylight-users group create <name>
straylight-users group add-member <group> <username>
straylight-users group remove-member <group> <username>
```

### `password`

Manage passwords.

```
straylight-users password <username> [--expire] [--reset]
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--json` | JSON output |
| `--verbose` | Debug logging |

## CONFIGURATION

```toml
[users]
default_shell = "/bin/zsh"
default_role = "user"
home_base = "/home"
skeleton = "/etc/skel"

[password]
min_length = 12
require_complexity = true
max_age_days = 90

[ldap]
enabled = false
server = "ldap://directory.local"
base_dn = "dc=example,dc=com"

[roles.admin]
permissions = ["*"]
quota_override = true

[roles.developer]
permissions = ["tools.*", "garden.*", "sandbox.*"]
```

## EXAMPLES

Create a developer user:

```
straylight-users add alice --role developer --groups "dev,docker"
```

List active sessions:

```
straylight-users session list
```

Show user details:

```
straylight-users info alice --json
```

Create a custom role:

```
straylight-users role create data-scientist --permissions "tools.mesh,tools.garden,tools.capsule"
```

Add a user to a group:

```
straylight-users group add-member gpu-users alice
```

Expire a password:

```
straylight-users password bob --expire
```

## INTEGRATION

- **straylight-quota**: User quotas are enforced based on role and group.
- **straylight-vault**: User credentials are stored in vault.
- **straylight-policy**: User roles determine applicable system policies.
- **straylight-remote**: Remote access uses user identity for authorization.
- **straylight-shield**: User account security is audited.
- **straylight-timeline**: User logins and actions are recorded.

## SEE ALSO

straylight-quota(1), straylight-vault(1), straylight-policy(1), straylight-remote(1), straylight-shield(1)
