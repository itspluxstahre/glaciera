# Glaciera

**Fuzzy. Ferocious. Terminal-born. Welcome to Glaciera.**

A modern, terminal-based music player and library manager with fuzzy search, playlist management, and a sleek ncurses interface.

[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-blue.svg)](https://www.gnu.org/licenses/gpl-2.0.html)

## About

Glaciera is a fork and modernization of **mp3berg** by Krister Brus, which itself was based on the original **jb** jukebox by Kristian Wiklund. This project brings the classic terminal music player into the modern era with:

- **Modern configuration system** using TOML format
- **XDG Base Directory compliance** for proper file locations
- **Theme support** with full RGB color customization
- **Multiple music directory support** for complex music libraries
- **Enhanced fuzzy search** and playlist management
- **SQLite database backend** for fast music library indexing

## Installation

### Prerequisites

- C compiler (gcc or clang)
- Meson build system
- ncurses with wide character support
- SQLite3 development libraries
- Audio libraries: libvorbis, libogg, libflac
- mpg123 and ogg123 for playback

### Build and Install

```bash
# Clone the repository
git clone https://github.com/itspluxstahre/glaciera.git
cd glaciera

# Configure and build
meson setup builddir
ninja -C builddir

# Install (optional)
sudo ninja -C builddir install
```

### Development

During development, run uninstalled binaries from the build directory:

```bash
# Build changes
ninja -C builddir

# Run Glaciera (music player)
./builddir/src/glaciera

# Run indexer (scan music library)
./builddir/src/glaciera-indexer /path/to/your/music
```

## Configuration

Glaciera uses a modern TOML-based configuration system with XDG compliance:

- **Config file**: `~/.config/glaciera/config.toml`
- **Themes**: `~/.config/glaciera/themes/`
- **Database**: `~/.local/share/glaciera/glaciera.db`

### First Run

On first launch, Glaciera automatically creates the configuration directory and a default `config.toml` file with helpful examples.

### Multiple Music Directories

Configure multiple music directories in your `config.toml`:

```toml
[paths]
index = [
    "/home/user/Music",
    "/mnt/nas/music",
    "/media/external/audio",
]
```

Then index them:
```bash
glaciera-indexer /home/user/Music /mnt/nas/music /media/external/audio
```

### Themes

Glaciera supports full RGB color themes. Copy theme files from `themes/` to `~/.config/glaciera/themes/` and select them in your config:

```toml
[appearance]
theme = "nord"  # or "gruvbox"
```

Included themes:
- **Nord** (default) - Modern arctic-inspired palette
- **Gruvbox** - Warm retro groove colors

See [`CONFIG.md`](CONFIG.md) for complete configuration documentation.

## Usage

### Basic Controls

| Key | Action |
|-----|--------|
| ↑↓ or j/k | Navigate through music library |
| ←→ or h/l | Adjust volume |
| p | Play selected track |
| P | Play all tagged tracks |
| r | Shuffle and play tagged tracks |
| t | Tag/untag track for playlist |
| ~ | Select/deselect all tracks |
| v | View current playlist |
| a | Add track to playlist |
| esc | Stop playback |
| q | Quit |

### Advanced Features

- **Fuzzy Search**: Type to search through your music library instantly
- **Playlist Management**: Create, save, and manage playlists
- **Volume Control**: Adjust playback volume with visual feedback
- **Tag System**: Mark favorite tracks for easy playlist creation

### Indexing Your Music

Before using Glaciera, index your music library:

```bash
# Index a single directory
glaciera-indexer /path/to/music

# Index multiple directories
glaciera-indexer /home/music /mnt/nas/music

# Force re-indexing (ignore cache)
glaciera-indexer -f /path/to/music
```

## Project History

Glaciera continues a long tradition of terminal-based music players:

1. **jb** (1997) - Original curses jukebox by Kristian Wiklund
2. **mp3berg** (2000-2010) - Enhanced version by Krister Brus with MP3 support
3. **Glaciera** (2025) - Modern fork by Plux Stahre with TOML config and themes

Each iteration builds upon the previous while adding modern conveniences and maintaining the core philosophy: a powerful, keyboard-driven music player that runs anywhere.

## Contributing

Contributions are welcome! Areas where help is needed:

- Theme creation and color palette design
- Platform-specific testing (Windows, macOS, BSD)
- Documentation improvements
- Bug reports and feature requests

### Code Formatting

Before submitting a pull request, please format all code using clang-format:

```bash
ninja -C builddir format
```

This ensures consistent code style across the project. The formatting rules are defined in `.clang-format` (WebKit style with 8-space indentation).

See [`MIGRATION.md`](MIGRATION.md) for technical details about the v4.0 modernization.

## Credits

See [`CREDITS.md`](CREDITS.md) for detailed attribution to all contributors and third-party libraries.

## License

This project is licensed under the GNU General Public License v2.0 - see the [COPYING](COPYING) file for details.

---

*Glaciera: Because sometimes you just want to queue up your entire music library and let the fuzzy search do the work.* 🎵
