# straylight-display

## NAME

straylight-display -- display configuration for resolution, refresh rate, HDR, and multi-monitor layout

## SYNOPSIS

```
straylight-display [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-display manages all display outputs on StrayLight OS. It handles resolution selection, refresh rate configuration, HDR tone mapping, multi-monitor arrangement, scaling, rotation, and color depth. The tool communicates directly with the DRM/KMS kernel subsystem and the Wayland compositor for seamless configuration changes without session restarts.

display supports hot-plug detection and automatic configuration. When a new monitor is connected, it reads the EDID data, selects the optimal mode, and positions the display based on user-defined rules or layout templates. Users can save and restore monitor layouts for different scenarios (docked, presentation, standalone).

The tool also manages variable refresh rate (FreeSync/G-Sync), fractional scaling, and per-display ICC color profiles in coordination with straylight-color.

## COMMANDS

### `list`

List connected displays and their capabilities.

```
straylight-display list [--json] [--verbose]
```

### `set`

Configure a display.

```
straylight-display set <display> --resolution <WxH> [--rate <Hz>] [--scale <factor>]
```

### `layout`

Manage multi-monitor layouts.

```
straylight-display layout show
straylight-display layout set <display> --position <x,y>
straylight-display layout save <name>
straylight-display layout load <name>
```

### `hdr`

Manage HDR settings.

```
straylight-display hdr <display> [--enable|--disable] [--brightness <nits>]
```

### `rotate`

Rotate a display.

```
straylight-display rotate <display> <0|90|180|270>
```

### `vrr`

Enable or disable variable refresh rate.

```
straylight-display vrr <display> [--enable|--disable]
```

### `off` / `on`

Turn a display off or on (DPMS).

```
straylight-display off <display>
straylight-display on <display>
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--json` | JSON output |
| `--apply` | Apply changes immediately (default) |
| `--test` | Test configuration for 10 seconds then revert |
| `--verbose` | Debug logging |

## CONFIGURATION

```toml
[display]
auto_configure = true
default_scale = 1.0

[hotplug]
auto_layout = true
prefer_position = "right"

[layouts.docked]
DP-1 = { resolution = "3840x2160", rate = 144, scale = 1.5, position = [0, 0] }
eDP-1 = { enabled = false }

[layouts.standalone]
eDP-1 = { resolution = "2560x1600", rate = 120, scale = 1.25, position = [0, 0] }

[hdr]
default_brightness = 400
tone_mapping = "pq"
```

## EXAMPLES

List all connected displays:

```
straylight-display list --verbose
```

Set resolution and refresh rate:

```
straylight-display set DP-1 --resolution 3840x2160 --rate 144
```

Save a layout:

```
straylight-display layout save docked
```

Load a layout:

```
straylight-display layout load standalone
```

Enable HDR:

```
straylight-display hdr DP-1 --enable --brightness 600
```

Test a configuration with auto-revert:

```
straylight-display set DP-1 --resolution 2560x1440 --rate 165 --test
```

## INTEGRATION

- **straylight-color**: Display color profiles are coordinated with the color manager.
- **straylight-fabric**: Display topology data for multi-GPU rendering decisions.
- **straylight-power**: Display brightness and DPMS are managed by power policies.
- **straylight-input**: Input device mapping is adjusted with display layout changes.
- **straylight-hub**: Hub's web UI adapts to display resolution changes.

## SEE ALSO

straylight-color(1), straylight-input(1), straylight-power(1), straylight-fabric(1)
