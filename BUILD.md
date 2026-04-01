# obs-twitch-ingest-benchmark — Build Instructions

## Prerequisites

### Common
- CMake ≥ 3.22
- Qt 6.x (same version OBS was built against — typically Qt 6.4+)
- C++17-capable compiler

### macOS
- Xcode Command Line Tools (`xcode-select --install`)
- Homebrew recommended

### Windows
- Visual Studio 2022 (MSVC v143) or later
- Qt 6 installer from https://www.qt.io

---

## macOS — Out-of-tree build

### 1. Install OBS Studio (binary release)

```sh
# Download the OBS Studio .dmg from https://obsproject.com and install it.
# The OBS SDK (headers + .dylib) ships inside the app bundle.
```

### 2. Locate OBS headers and libraries

```sh
# After installing OBS.app to /Applications:
export OBS_ROOT="/Applications/OBS.app/Contents"
# Headers are typically at $OBS_ROOT/Headers (or build from source below)
```

If you need the development headers, build OBS from source:

```sh
git clone --recursive https://github.com/obsproject/obs-studio.git
cd obs-studio
cmake -B build -DENABLE_BROWSER=OFF -DENABLE_VLC=OFF \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --config RelWithDebInfo
cmake --install build --prefix ~/obs-install
export OBS_ROOT="$HOME/obs-install"
```

### 3. Configure and build the plugin

```sh
cd obs-twitch-ingest-benchmark

cmake -B build \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DOBS_ROOT="$OBS_ROOT" \
  -DCMAKE_PREFIX_PATH="$OBS_ROOT;$(brew --prefix qt6)"

cmake --build build --config RelWithDebInfo
```

### 4. Install the plugin

```sh
PLUGIN_DIR="$HOME/Library/Application Support/obs-studio/plugins/obs-twitch-ingest-benchmark"
mkdir -p "$PLUGIN_DIR/bin"
cp build/libobs-twitch-ingest-benchmark.dylib "$PLUGIN_DIR/bin/"
```

Restart OBS. The **Twitch Ingest Benchmark** dock should appear under **Docks** menu.

---

## Windows — Out-of-tree build (Developer PowerShell / x64 Native Tools)

### 1. Build or download OBS development files

Download the OBS Studio Windows installer and install it.  
For headers, clone and build from source:

```powershell
git clone --recursive https://github.com/obsproject/obs-studio.git
cd obs-studio
cmake -B build -G "Visual Studio 17 2022" -A x64 `
      -DENABLE_BROWSER=OFF -DENABLE_VLC=OFF
cmake --build build --config RelWithDebInfo
cmake --install build --prefix C:\obs-install
$env:OBS_ROOT = "C:\obs-install"
```

### 2. Install Qt 6

Download the Qt Maintenance Tool from https://www.qt.io/download  
Install Qt 6.4+ with the **MSVC 2019 64-bit** component.

```powershell
$env:QT_ROOT = "C:\Qt\6.6.0\msvc2019_64"
```

### 3. Configure and build

```powershell
cd obs-twitch-ingest-benchmark

cmake -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH="$env:OBS_ROOT;$env:QT_ROOT" `
  -DOBS_ROOT="$env:OBS_ROOT"

cmake --build build --config RelWithDebInfo
```

### 4. Install the plugin

```powershell
$OBS_PLUGIN_DIR = "C:\Program Files\obs-studio\obs-plugins\64bit"
Copy-Item build\RelWithDebInfo\obs-twitch-ingest-benchmark.dll $OBS_PLUGIN_DIR
```

Restart OBS. The dock will appear under **Docks** → **Twitch Ingest Benchmark**.

---

## Building inside the OBS source tree (recommended for CI)

```sh
# Copy or symlink this plugin into obs-studio/plugins/
cp -r obs-twitch-ingest-benchmark /path/to/obs-studio/plugins/

# Add to obs-studio/plugins/CMakeLists.txt:
#   add_subdirectory(obs-twitch-ingest-benchmark)

# Then build OBS normally:
cmake --build /path/to/obs-studio/build
```

When built this way, the `obs_install_plugin()` macro places the binary in the
correct location automatically.

---

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| `Could not find OBS libraries` | Set `-DOBS_ROOT=` or add OBS install prefix to `CMAKE_PREFIX_PATH` |
| `Qt6 not found` | Set `-DCMAKE_PREFIX_PATH=/path/to/Qt/6.x.y/platform` |
| Plugin loads but dock is missing | Check OBS log (`Help → Log Files`) for `[obs-twitch-ingest-benchmark]` entries |
| Benchmark hangs | Check firewall — TCP 1935 to `*.live-video.net` must be reachable |
| `applyStreamServer failed` | Ensure OBS stream service is set to **Twitch** (not Custom RTMP) |
