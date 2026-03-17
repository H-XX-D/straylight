# straylight-input

## NAME

straylight-input -- input device configuration for keyboards, mice, tablets, and gamepads

## SYNOPSIS

```
straylight-input [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-input manages all input devices connected to StrayLight OS. It configures keyboard layouts, key remapping, mouse sensitivity and acceleration profiles, touchpad gestures, drawing tablet pressure curves, and gamepad mappings. The tool works with both libinput and direct evdev for maximum device compatibility.

input supports device-specific profiles that activate automatically when a device is connected. A user can have different sensitivity settings for their desktop mouse and their laptop touchpad, different key maps for their external and built-in keyboards, and custom button mappings for specific gamepads -- all applied automatically via udev matching.

The tool also provides a live input event viewer for debugging device issues and a calibration wizard for touchscreens and drawing tablets.

## COMMANDS

### `list`

List connected input devices.

```
straylight-input list [--json] [--verbose]
```

### `set`

Configure a device.

```
straylight-input set <device> <property> <value>
```

### `profile`

Manage device profiles.

```
straylight-input profile list
straylight-input profile create <name> --device <id>
straylight-input profile apply <name>
straylight-input profile show <name>
```

### `remap`

Remap keys or buttons.

```
straylight-input remap <device> --from <key> --to <key|sequence>
```

### `calibrate`

Run the calibration wizard for a device.

```
straylight-input calibrate <device>
```

### `test`

Show live input events from a device.

```
straylight-input test <device>
```

### `gesture`

Configure touchpad gestures.

```
straylight-input gesture list
straylight-input gesture set <gesture> --action <command>
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--json` | JSON output |
| `--device <id>` | Target device by ID or name |
| `--verbose` | Debug logging |

## CONFIGURATION

```toml
[input]
auto_profile = true

[keyboard.default]
layout = "us"
variant = ""
repeat_delay = 300
repeat_rate = 40

[keyboard.remap]
CapsLock = "Ctrl_L"

[mouse.default]
acceleration = "adaptive"
speed = 0.0
natural_scroll = false

[touchpad.default]
tap_to_click = true
natural_scroll = true
two_finger_scroll = true
gestures = true

[tablet."Wacom Intuos"]
pressure_curve = [[0, 0], [30, 10], [70, 90], [100, 100]]
area_mapping = "proportional"
```

## EXAMPLES

List all input devices:

```
straylight-input list --verbose
```

Set mouse speed:

```
straylight-input set "Logitech G502" speed 0.5
```

Remap Caps Lock to Ctrl:

```
straylight-input remap keyboard:0 --from CapsLock --to Ctrl_L
```

Configure a 3-finger swipe gesture:

```
straylight-input gesture set swipe-3-up --action "straylight-dash"
```

Test input events from a device:

```
straylight-input test "Wacom Intuos"
```

Calibrate a touchscreen:

```
straylight-input calibrate touchscreen:0
```

## INTEGRATION

- **straylight-display**: Input mapping adjusts with display layout changes.
- **straylight-policy**: Input configurations can be enforced by system policy.
- **straylight-users**: Per-user input profiles switch on login.
- **straylight-fabric**: Device topology shows input device connections.
- **straylight-echo**: Configuration changes are registered for undo.

## SEE ALSO

straylight-display(1), straylight-users(1), straylight-policy(1), straylight-fabric(1)
