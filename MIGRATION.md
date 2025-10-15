# Configuration System Migration - v4.0

This document describes the major configuration system refactor in Glaciera v4.0.

## Summary of Changes

### 1. XDG Base Directory Compliance

Glaciera now follows the XDG Base Directory specification:

**Old locations:**
- Config: `/etc/glacierarc`, `~/.glacierarc`
- Database: In music library directory (`/path/to/music/glaciera.db`)

**New locations:**
- Config: `~/.config/glaciera/config.toml`
- Themes: `~/.config/glaciera/themes/*.toml`
- Database: `~/.local/share/glaciera/glaciera.db`
- Cache: `~/.cache/glaciera/`

### 2. TOML Configuration Format

Replaced the old custom config format with modern TOML.

**Old format (.glacierarc):**
```
:datapath:
/path/to/music
:mp3playerpath:
mpg123
```

**New format (config.toml):**
```toml
[paths]
# Support for multiple directories
index = [
    "/path/to/music",
    "/another/music/directory",
]

[players]
mp3_player = "mpg123"
```

### 3. Theme System

New JSON-like TOML theme system with RGB color support:
- Themes stored in `~/.config/glaciera/themes/`
- Full RGB color specification for each UI element
- Automatic fallback for limited terminals
- Two example themes included: Nord and Gruvbox

### 4. Code Architecture

**New files:**
- `src/config.c` - Modern configuration system
- `src/config.h` - Configuration API
- `src/toml.c` - TOML parser (tomlc99)
- `src/toml.h` - TOML parser header

**Modified files:**
- `src/common.c` - Removed old config parsing
- `src/common.h` - Kept legacy variables as compatibility shims
- `src/glaciera.c` - Uses new config system, enhanced theme support
- `src/glaciera-indexer.c` - Uses new config system
- `src/meson.build` - Added new source files

**Removed functionality:**
- `parse_rc_file()` function
- `read_rc_file()` function  
- `sanitize_rc_parameters()` function
- Support for `/etc/glacierarc` and `~/.glacierarc`

## Migration Steps

### For Users

1. **First Run**: Glaciera will automatically create `~/.config/glaciera/` and generate a default `config.toml`
2. **Configure Paths**: Edit `~/.config/glaciera/config.toml` and add all your music directories to the `index` array
3. **Re-index**: Run `glaciera-indexer` with all your music paths to rebuild the database in its new location
4. **Themes**: Copy theme files from `themes/` to `~/.config/glaciera/themes/` and select in config

**Example first run workflow:**
```bash
# Run glaciera (will create config)
glaciera

# Edit config to add your music paths
nano ~/.config/glaciera/config.toml

# Index your music
glaciera-indexer /path/to/music1 /path/to/music2
```

### For Developers

Legacy `opt_*` variables in `common.h` are still present and populated from the new config system for backward compatibility with existing code that references them. New code should use `global_config` from `config.h` directly.

## Benefits

1. **Standards Compliance**: Follows XDG Base Directory spec
2. **Modern Format**: TOML is human-readable and well-supported
3. **Multiple Directories**: Index music from multiple locations simultaneously
4. **Themeable**: Full RGB color customization
5. **Extensible**: Easy to add new configuration options
6. **Clean Separation**: Config, data, and cache in separate directories
7. **No Root**: No more `/etc` dependency
8. **Auto-creation**: Config directory and default file created automatically

## Breaking Changes

- Old `.glacierarc` files are not read
- Database must be regenerated in new location
- System-wide `/etc/glacierarc` no longer supported

## Future Enhancements

Possible future additions:
- Runtime theme switching (no restart required)
- Theme gallery/repository
- Import themes from other apps (kitty, alacritty, etc.)
- Per-user theme overrides
- Color scheme validation and previews
