# straylight-color

## NAME

straylight-color -- color management with ICC profile support and night-shift scheduling

## SYNOPSIS

```
straylight-color [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-color manages display color accuracy across StrayLight OS. It applies ICC (International Color Consortium) profiles to displays, manages color temperature for eye comfort (night shift / blue light reduction), and ensures consistent color reproduction for creative workflows. The tool integrates with the Wayland compositor's color management protocol for hardware-accurate color.

For creative professionals, color provides per-display ICC profile assignment with hardware LUT (lookup table) loading when the display supports it. For general users, it provides automatic blue light reduction that follows the solar cycle or a custom schedule.

color also manages system-wide color themes, providing a consistent dark/light mode toggle that all StrayLight tools and applications respect.

## COMMANDS

### `profile`

Manage ICC profiles.

```
straylight-color profile list [--display <name>]
straylight-color profile set <display> <profile-path>
straylight-color profile calibrate <display>
```

### `temperature`

Manage color temperature / night shift.

```
straylight-color temperature [<kelvin>]
straylight-color temperature auto [--location <lat,lon>]
straylight-color temperature schedule --day <kelvin> --night <kelvin> --transition <duration>
```

### `theme`

Manage system color theme.

```
straylight-color theme [dark|light|auto]
straylight-color theme accent <color>
```

### `status`

Show current color state.

```
straylight-color status [--json]
```

### `reset`

Reset color settings to defaults.

```
straylight-color reset [<display>]
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--json` | JSON output |
| `--verbose` | Debug logging |

## CONFIGURATION

```toml
[color]
theme = "dark"
accent = "#7C3AED"

[night_shift]
enabled = true
mode = "auto"               # auto | schedule | manual
location = [37.7749, -122.4194]
day_temperature = 6500
night_temperature = 3500
transition_duration = "30m"

[profiles]
DP-1 = "/usr/share/color/icc/dell-u2723qe.icc"
eDP-1 = "srgb"

[calibration]
hardware_lut = true
rendering_intent = "relative-colorimetric"
```

## EXAMPLES

Set night shift to 3500K:

```
straylight-color temperature 3500
```

Enable automatic night shift:

```
straylight-color temperature auto --location 37.7749,-122.4194
```

Switch to dark theme:

```
straylight-color theme dark
```

Set a display's ICC profile:

```
straylight-color profile set DP-1 ~/calibration/monitor.icc
```

Set the accent color:

```
straylight-color theme accent "#7C3AED"
```

Check current status:

```
straylight-color status
```

## INTEGRATION

- **straylight-display**: Display configuration coordinates with color profiles.
- **straylight-fonts**: Font rendering quality depends on color profile accuracy.
- **straylight-power**: Night shift adjusts based on power policy.
- **straylight-users**: Per-user color preferences switch on login.
- **straylight-hub / straylight-dash**: Dashboards respect the system theme.

## SEE ALSO

straylight-display(1), straylight-fonts(1), straylight-power(1), straylight-users(1)
