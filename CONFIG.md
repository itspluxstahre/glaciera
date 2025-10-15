# Glaciera Configuration Guide

Glaciera now uses a modern XDG-compliant configuration system with TOML format.

## Configuration Locations

Following XDG Base Directory specifications:

- **Config directory**: `~/.config/glaciera/`
- **Config file**: `~/.config/glaciera/config.toml`
- **Themes directory**: `~/.config/glaciera/themes/`
- **Database**: `~/.local/share/glaciera/glaciera.db`
- **Cache**: `~/.cache/glaciera/`

On first run, Glaciera will automatically create these directories and a default `config.toml` file.

## Configuration File Format

The configuration file uses TOML syntax. Here's an example:

```toml
# Glaciera Configuration File

[paths]
# Directories to index for music files (array of paths)
# You can add multiple directories here
index = [
    "/Users/username/Music",
    "/mnt/music",
    "/media/external/audio",
]

rippers = "/Users/username/Music/rippers"

[players]
mp3_player = "mpg123"
mp3_flags = ""
ogg_player = "ogg123"
ogg_flags = ""

[appearance]
# Theme name (default, or filename from themes/ directory without .toml)
theme = "default"
```

### Multiple Index Paths

Glaciera now supports indexing multiple music directories. Simply add them to the `index` array in the `[paths]` section:

```toml
[paths]
index = [
    "/home/user/Music",
    "/mnt/nas/music",
    "/media/usb/audio",
]
```

All paths will be scanned and indexed when you run `glaciera-indexer`.

## Creating Custom Themes

Themes are stored as TOML files in `~/.config/glaciera/themes/`. Each theme defines RGB color values for different UI elements.

### Theme File Structure

```toml
name = "My Theme Name"

[colors]
# Main window background
[colors.main_bg]
r = 46
g = 52
b = 64

# Main window foreground (text)
[colors.main_fg]
r = 216
g = 222
b = 233

# Status bar background
[colors.accent_bg]
r = 94
g = 129
b = 172

# Status bar foreground
[colors.accent_fg]
r = 236
g = 239
b = 244

# Now playing track color
[colors.playing]
r = 235
g = 203
b = 139

# Playlist items color
[colors.playlist]
r = 163
g = 190
b = 140

# Selection highlight background
[colors.highlight_bg]
r = 236
g = 239
b = 244

# Selection highlight foreground
[colors.highlight_fg]
r = 46
g = 52
b = 64
```

### Using a Theme

1. Create your theme file (e.g., `~/.config/glaciera/themes/mytheme.toml`)
2. Edit `~/.config/glaciera/config.toml` and set:
   ```toml
   [appearance]
   theme = "mytheme"
   ```
3. Restart Glaciera

### Included Themes

Glaciera ships with these example themes in the source tree's `themes/` directory:

- **nord** - Nord dark theme (default)
- **gruvbox** - Gruvbox dark theme

Copy them to `~/.config/glaciera/themes/` to use them.

## Color Support

- **RGB Colors**: If your terminal supports color customization (256 colors or more), Glaciera will use the exact RGB values from your theme.
- **Fallback**: On terminals with limited color support, Glaciera falls back to standard ANSI colors.

## Migration from Old Config

The old `.glacierarc` format is no longer supported. On first run, Glaciera will create a new config file with default values. You'll need to manually copy your settings to the new `config.toml` format.

## Database Location

The database has been moved from your music directory to `~/.local/share/glaciera/`. You'll need to re-index your music library after upgrading:

```bash
glaciera-indexer /path/to/your/music
```

## Examples

### Using the Nord Theme

```bash
# Copy the theme file
cp themes/nord.toml ~/.config/glaciera/themes/

# Edit config
nano ~/.config/glaciera/config.toml
# Set: theme = "nord"
```

### Using the Gruvbox Theme

```bash
# Copy the theme file
cp themes/gruvbox.toml ~/.config/glaciera/themes/

# Edit config
nano ~/.config/glaciera/config.toml
# Set: theme = "gruvbox"
```

### Indexing Multiple Music Directories

Edit `~/.config/glaciera/config.toml`:

```toml
[paths]
index = [
    "/home/user/Music",
    "/mnt/nas/music",
    "/media/external/music",
]
```

Then index all directories:

```bash
# Index all paths defined in config
glaciera-indexer /home/user/Music /mnt/nas/music /media/external/music

# Or index them individually
glaciera-indexer /home/user/Music
glaciera-indexer /mnt/nas/music
```
