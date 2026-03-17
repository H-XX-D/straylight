# straylight-network

## NAME

straylight-network -- network management for interfaces, routing, DNS, and VPN tunnels

## SYNOPSIS

```
straylight-network [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-network is the primary network management tool for StrayLight OS. It configures network interfaces, manages IP addressing (static and DHCP), controls routing tables, configures DNS resolution, and establishes VPN tunnels. It replaces NetworkManager and systemd-networkd with a unified tool that integrates deeply with the StrayLight ecosystem.

network supports wired Ethernet, Wi-Fi (with WPA3 and EAP), bonding, bridging, VLANs, WireGuard VPN, and OpenVPN. Configuration can be applied interactively or declaratively from TOML files. Changes are atomic -- a failed configuration is rolled back automatically, and the previous working state is restored.

The tool also provides built-in connectivity checking, captive portal detection, and automatic fallback between connections (e.g., failing over from wired to wireless).

## COMMANDS

### `list`

List network interfaces and their status.

```
straylight-network list [--json] [--verbose]
```

### `up` / `down`

Bring an interface up or down.

```
straylight-network up <interface>
straylight-network down <interface>
```

### `connect`

Connect to a Wi-Fi network.

```
straylight-network connect <ssid> [--password <pass>] [--security <wpa3|wpa2|open>]
```

### `config`

Configure an interface.

```
straylight-network config <interface> --ip <address/prefix> [--gateway <ip>] [--dns <servers>]
straylight-network config <interface> --dhcp
```

### `dns`

Manage DNS configuration.

```
straylight-network dns show
straylight-network dns set <servers...>
straylight-network dns flush
```

### `route`

Manage routing table.

```
straylight-network route list
straylight-network route add <destination> via <gateway> [--dev <interface>]
straylight-network route delete <destination>
```

### `vpn`

Manage VPN connections.

```
straylight-network vpn list
straylight-network vpn connect <name>
straylight-network vpn disconnect <name>
straylight-network vpn create <name> --type <wireguard|openvpn> --config <path>
```

### `scan`

Scan for available Wi-Fi networks.

```
straylight-network scan [--json]
```

### `bond` / `bridge` / `vlan`

Create network aggregation interfaces.

```
straylight-network bond create <name> --members <interfaces> [--mode <802.3ad|active-backup>]
straylight-network bridge create <name> --members <interfaces>
straylight-network vlan create <name> --parent <interface> --id <vlan-id>
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--json` | JSON output |
| `--timeout <duration>` | Connection timeout |
| `--verbose` | Debug logging |

## CONFIGURATION

```toml
[network]
hostname = "straylight-workstation"
manage_dns = true
connectivity_check = true

[interfaces.eth0]
method = "dhcp"
priority = 100

[interfaces.wlan0]
method = "dhcp"
priority = 50
auto_connect = true

[dns]
servers = ["1.1.1.1", "8.8.8.8"]
search = ["local"]
dnssec = true

[vpn.work]
type = "wireguard"
config = "/etc/straylight/vpn/work.conf"
auto_connect = false
```

## EXAMPLES

List network interfaces:

```
straylight-network list
```

Connect to Wi-Fi:

```
straylight-network connect "MyNetwork" --password "secret" --security wpa3
```

Configure a static IP:

```
straylight-network config eth0 --ip 192.168.1.100/24 --gateway 192.168.1.1 --dns 1.1.1.1
```

Start a WireGuard VPN:

```
straylight-network vpn connect work
```

Create a bonded interface:

```
straylight-network bond create bond0 --members eth0,eth1 --mode 802.3ad
```

Flush DNS cache:

```
straylight-network dns flush
```

## INTEGRATION

- **straylight-probe**: Network diagnostics tool uses network's configuration.
- **straylight-remote**: Remote access operates over configured network interfaces.
- **straylight-shield**: Network configuration is audited for security.
- **straylight-health**: Network connectivity contributes to health score.
- **straylight-power**: Wi-Fi power saving is managed by power policies.
- **straylight-echo**: Network configuration changes are registered for undo.

## SEE ALSO

straylight-probe(1), straylight-remote(1), straylight-shield(1), straylight-power(1), straylight-health(1)
