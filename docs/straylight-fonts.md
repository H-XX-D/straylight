# straylight-fonts

## NAME

straylight-fonts -- font management including discovery, installation, and rendering hints

## SYNOPSIS

```
straylight-fonts [OPTIONS] COMMAND [ARGS...]
```

## DESCRIPTION

straylight-fonts manages the font ecosystem on StrayLight OS. It handles font discovery, installation, removal, configuration of rendering hints (antialiasing, hinting, subpixel rendering), and provides a searchable font catalog. The tool manages both system-wide fonts and per-user font collections.

fonts integrates with fontconfig under the hood but provides a friendlier interface. It can install fonts from local files, directories, or a curated online repository. It also detects and resolves font conflicts, missing glyphs, and rendering issues.

The tool supports variable fonts, color emoji fonts, and CJK font fallback chains. It can generate font previews and provide detailed metadata about installed fonts including supported character ranges, OpenType features, and license information.

## COMMANDS

### `list`

List installed fonts.

```
straylight-fonts list [--family <name>] [--style <style>] [--json]
```

### `install`

Install fonts.

```
straylight-fonts install <path|url|name> [--user|--system]
```

### `remove`

Remove fonts.

```
straylight-fonts remove <family> [--style <style>]
```

### `search`

Search the font catalog.

```
straylight-fonts search <query> [--lang <language>] [--category <category>]
```

### `info`

Show detailed font information.

```
straylight-fonts info <family> [--verbose]
```

### `preview`

Generate a font preview.

```
straylight-fonts preview <family> [--text <sample>] [--size <pt>] [--output <path>]
```

### `config`

Configure font rendering.

```
straylight-fonts config [--antialias <on|off>] [--hinting <full|medium|slight|none>] [--subpixel <rgb|bgr|none>]
```

### `fallback`

Manage fallback chains.

```
straylight-fonts fallback show
straylight-fonts fallback set <language> <family-list>
```

### `refresh`

Rebuild the font cache.

```
straylight-fonts refresh
```

## OPTIONS

| Option | Description |
|--------|-------------|
| `--config <path>` | Configuration file path |
| `--user` | User-level operation |
| `--system` | System-level operation |
| `--json` | JSON output |
| `--verbose` | Debug logging |

## CONFIGURATION

```toml
[fonts]
default_sans = "Inter"
default_serif = "Noto Serif"
default_mono = "JetBrains Mono"

[rendering]
antialias = true
hinting = "slight"
subpixel = "rgb"
lcdfilter = "default"

[fallback]
emoji = ["Noto Color Emoji"]
cjk = ["Noto Sans CJK"]

[directories]
system = ["/usr/share/fonts"]
user = ["~/.local/share/fonts"]
```

## EXAMPLES

List all monospace fonts:

```
straylight-fonts list --category monospace
```

Install a font from a file:

```
straylight-fonts install ~/Downloads/JetBrainsMono.zip --user
```

Search for CJK fonts:

```
straylight-fonts search "noto" --lang ja
```

Preview a font:

```
straylight-fonts preview "Inter" --text "The quick brown fox" --size 24
```

Configure rendering:

```
straylight-fonts config --hinting slight --subpixel rgb
```

Set a CJK fallback chain:

```
straylight-fonts fallback set ja "Noto Sans CJK JP,IPAGothic"
```

## INTEGRATION

- **straylight-color**: Font rendering interacts with color profiles.
- **straylight-display**: Font scaling adjusts with display DPI.
- **straylight-garden**: Environments can include their own font sets.
- **straylight-mirror**: Font configuration is preserved during cloning.
- **straylight-policy**: Font installation can be restricted by policy.

## SEE ALSO

straylight-color(1), straylight-display(1), straylight-policy(1)
