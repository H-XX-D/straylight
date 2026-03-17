# straylight-audio

## NAME

straylight-audio -- audio routing engine with per-application sink/source control

## SYNOPSIS

```
straylight-audio [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-audio manages audio input and output on StrayLight OS. It builds on PipeWire to provide per-application volume control, audio routing between sources and sinks, sample rate management, latency tuning, and effects processing. The tool gives users complete control over which applications output to which audio devices, with the ability to create virtual sinks for routing and mixing.

audio supports complex routing scenarios: splitting an application's output to both headphones and a recording sink, merging multiple microphone inputs, routing system sounds to one device while media plays on another. These configurations are saved as profiles and can be switched instantly.

The tool also manages MIDI device routing and provides a real-time audio level meter for monitoring and debugging audio issues.

## COMMANDS

### `list`

List audio devices, streams, and routes.

```
straylight-audio list [--devices] [--streams] [--routes] [--json]
```

### `set`

Configure an audio device or stream.

```
straylight-audio set <device|stream> <property> <value>
```

### `route`

Create an audio route between source and sink.

```
straylight-audio route <source> --to <sink>
```

### `volume`

Get or set volume.

```
straylight-audio volume [<device|stream>] [<level>] [--mute|--unmute]
```

### `profile`

Manage audio profiles.

```
straylight-audio profile list
straylight-audio profile save <name>
straylight-audio profile load <name>
```

### `meter`

Show real-time audio levels.

```
straylight-audio meter [<device|stream>]
```

### `virtual`

Create or remove virtual audio devices.

```
straylight-audio virtual create <name> --type <sink|source>
straylight-audio virtual remove <name>
```

### `latency`

Configure audio latency.

```
straylight-audio latency [<device>] [--buffer <frames>] [--period <frames>]
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--json` | JSON output |
| `--verbose` | Debug logging |

## CONFIGURATION

```toml
[audio]
default_sink = "auto"
default_source = "auto"
sample_rate = 48000
default_buffer = 1024

[profiles.studio]
sink = "USB Audio Interface"
source = "USB Audio Interface"
sample_rate = 96000
buffer = 256

[profiles.meeting]
sink = "Built-in Speakers"
source = "Headset Microphone"

[volume]
default = 80
max = 100
normalize = true
```

## EXAMPLES

List all audio streams:

```
straylight-audio list --streams
```

Set volume for Firefox:

```
straylight-audio volume "Firefox" 60
```

Route music player to headphones:

```
straylight-audio route "Spotify" --to "Headphones"
```

Create a virtual sink for recording:

```
straylight-audio virtual create recording-mix --type sink
straylight-audio route "Firefox" --to recording-mix
straylight-audio route "Discord" --to recording-mix
```

Switch to the studio profile:

```
straylight-audio profile load studio
```

Monitor levels:

```
straylight-audio meter
```

## INTEGRATION

- **straylight-display**: Audio device follows display for HDMI/DP outputs.
- **straylight-sandbox**: Sandboxed applications get controlled audio access.
- **straylight-users**: Per-user audio profiles switch on login.
- **straylight-notify**: Notification sounds are routed through audio.
- **straylight-power**: Audio devices are powered down on idle.
- **straylight-fabric**: Audio device topology is tracked.

## SEE ALSO

straylight-display(1), straylight-input(1), straylight-sandbox(1), straylight-users(1), straylight-power(1)
