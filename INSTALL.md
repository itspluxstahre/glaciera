# Build & Install Instructions

## Prerequisites

Before building glaciera, you'll need to install the required dependencies. The project uses **Meson** as its build system and requires development headers for several audio libraries.

## Installing Dependencies

### Linux (Debian/Ubuntu)

```bash
sudo apt update
sudo apt install build-essential meson ninja-build \
    libncursesw5-dev libogg-dev libvorbis-dev libflac-dev
```

### Linux (Fedora/CentOS/RHEL)

```bash
sudo dnf install gcc meson ninja-build \
    ncurses-devel libogg-devel libvorbis-devel flac-devel
```

### Linux (Arch Linux)

```bash
sudo pacman -S base-devel meson \
    ncurses libogg libvorbis flac
```

### macOS

Using [Homebrew](https://brew.sh/):

```bash
# Install Homebrew if not already installed
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install build tools and dependencies
brew install meson ninja ncurses libogg libvorbis flac
```

### FreeBSD

```bash
sudo pkg install meson ninja ncurses libogg libvorbis flac
```

## Building and Installation

Once you have the dependencies installed, you can build and install glaciera:

```bash
# Configure the build (this creates a 'builddir' directory)
meson setup builddir

# Build the project
ninja -C builddir

# Install system-wide (requires administrator privileges)
sudo ninja -C builddir install
```

## Development Usage

During development, you can run the binaries directly from the build directory without installing them:

```bash
# Run glaciera from build directory
./builddir/src/glaciera

# Run glaciera-indexer from build directory
./builddir/src/glaciera-indexer
```

## Runtime Dependencies

For actual audio playback, you'll also need runtime tools:

### Linux
```bash
sudo apt install mpg123 vorbis-tools  # For MP3 and OGG playback
```

### macOS
```bash
brew install mpg123 libvorbis
```

## Troubleshooting

### Common Issues

1. **"ncurses library not found"**: Make sure you have the development headers installed (e.g., `libncursesw5-dev` on Ubuntu)

2. **"Meson not found"**: Install Meson using your package manager or pip: `pip install meson`

3. **Permission denied during install**: Make sure you have sudo access or install to a directory you own:
   ```bash
   meson setup builddir --prefix=$HOME/.local
   ninja -C builddir install
   ```

### Building for Different Architectures

For cross-compilation or building for different architectures, refer to the [Meson documentation](https://mesonbuild.com/Cross-compilation.html).

## Configuration

After installation, you may want to:

1. Copy `global_glacierarc` to `/etc/glacierarc` for system-wide configuration
2. Create `~/.glacierarc` for personal configuration
3. Set up your music directory in the configuration file

See the `glacierarc` file for configuration options.
