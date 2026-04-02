# obs-twitch-ingest-benchmark

An OBS Studio plugin that benchmarks all Twitch ingest servers and automatically applies the fastest one before you start streaming.

## Features

- Fetches the live Twitch ingest server list from the Twitch API
- Measures DNS resolution latency and TCP connect time across multiple rounds per server
- Scores servers by a weighted combination of latency, jitter, reliability, and DNS performance
- Displays results in a sortable table inside an OBS dock
- **Auto-benchmark mode**: intercepts the OBS "Start Streaming" button, runs the benchmark first, applies the best server, then starts the stream — no extra clicks required
- EU-only filter for streamers who want to restrict results to European servers
- Settings are persisted across OBS restarts

---

## Prerequisites

### For using the plugin (pre-built release)

| Requirement | Details |
|---|---|
| **OBS Studio 30.0 or later** | Download from [obsproject.com](https://obsproject.com) |
| **macOS** | Apple Silicon (arm64) only |
| **Windows** | 64-bit, Windows 10 or later |

> The plugin does **not** support Intel Macs or Linux in the pre-built releases. Build from source for those platforms.

### For building from source

| Requirement | macOS | Windows |
|---|---|---|
| **OBS Studio** | OBS.app installed at `/Applications/OBS.app` | Built from source (see below) |
| **OBS source headers** | Cloned from GitHub | Cloned from GitHub |
| **CMake** | 3.22 or later | 3.22 or later |
| **Qt 6** | Homebrew `qt@6` | Qt 6.8 via [aqtinstall](https://github.com/miurahr/aqtinstall) or Qt Maintenance Tool |
| **Compiler** | Xcode Command Line Tools (Xcode 14+) | Visual Studio 2022 (MSVC v143) |
| **simde** | Homebrew `simde` | Bundled with OBS deps (auto-downloaded) |
| **libcurl** | System (macOS ships curl) | vcpkg `curl:x64-windows-static-release` |
| **vcpkg** | Not required | Pre-installed on windows-2022 CI runner; install manually for local builds |

---

## Installation (pre-built releases)

Download the latest release for your platform from the [Releases](../../releases) page.

### macOS (Apple Silicon)

1. Download `obs-twitch-ingest-benchmark-macos-arm64.zip`
2. Unzip — you get `obs-twitch-ingest-benchmark.plugin`
3. Copy the `.plugin` bundle to:
   ```
   ~/Library/Application Support/obs-studio/plugins/
   ```
4. Restart OBS

### Windows (x64)

1. Download `obs-twitch-ingest-benchmark-windows.zip`
2. Unzip — you get `obs-twitch-ingest-benchmark.dll`
3. Copy the `.dll` to your OBS plugins folder:
   ```
   C:\Program Files\obs-studio\obs-plugins\64bit\
   ```
4. Restart OBS

After installation, the **Twitch Ingest Benchmark** dock appears under **Docks** in the OBS menu bar. If it is not visible, enable it via **Docks → Twitch Ingest Benchmark**.

---

## Usage

### Running a benchmark

1. Open the **Twitch Ingest Benchmark** dock (Docks menu → Twitch Ingest Benchmark)
2. Optionally check **Only test EU servers** to limit the test to European ingest points
3. Click **Run Benchmark**
4. The progress bar shows how many servers have been tested; the log shows live status
5. When finished, the results table lists every server sorted by score — the recommended server is highlighted

### Applying the best server

After a benchmark completes:

- Click **Apply Best Server** to write the top-ranked server to your OBS stream settings, or
- Select any row in the results table and click **Apply Best Server** to apply that specific server instead

> **Note:** OBS must be configured with a **Twitch** stream service (not Custom RTMP) for the server to be applied correctly.

### Auto-benchmark before streaming

1. Check **Auto-benchmark and apply best server when starting stream**
2. Click the normal OBS **Start Streaming** button as usual
3. The plugin intercepts the click, runs a fresh benchmark, applies the best server, then starts the stream automatically

You can also trigger this sequence manually at any time with the **Benchmark & Stream** button.

### Results table columns

| Column | Description |
|---|---|
| Server | Twitch ingest server name and region |
| DNS (ms) | DNS resolution time in milliseconds |
| TCP min/mean/max | Best, average, and worst TCP connect latency across probe rounds |
| Jitter | Standard deviation of TCP latencies (lower = more stable) |
| Reliability | Percentage of probe rounds that succeeded |
| Score | Composite score (higher is better); weights: latency 40%, jitter 25%, reliability 25%, DNS 10% |

---

## Building from source

### macOS (Apple Silicon)

**Step 1 — Install prerequisites**

```sh
xcode-select --install
brew install cmake qt@6 simde
```

**Step 2 — Install OBS Studio**

Download and install OBS from [obsproject.com](https://obsproject.com). The plugin build system reads frameworks and libraries directly from `/Applications/OBS.app`.

**Step 3 — Clone OBS source headers**

The plugin needs OBS header files (`obs-module.h`, `obs-frontend-api.h`, etc.) from the OBS source tree. These are not included in OBS.app.

```sh
git clone --depth 1 --branch 32.1.0 \
  https://github.com/obsproject/obs-studio.git /tmp/obs-headers
```

**Step 4 — Clone and build this plugin**

```sh
git clone https://github.com/StephanBeutel/obs-twitch-ingest-benchmark.git
cd obs-twitch-ingest-benchmark

cmake -B build \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DOBS_APP=/Applications/OBS.app \
  -DOBS_SOURCE_DIR=/tmp/obs-headers \
  -DQt6_DIR="$(brew --prefix qt@6)/lib/cmake/Qt6"

cmake --build build --config RelWithDebInfo --parallel

# Install to ~/Library/Application Support/obs-studio/plugins/
cmake --install build
```

Restart OBS to pick up the plugin.

---

### Windows (x64)

**Step 1 — Install prerequisites**

- [Visual Studio 2022](https://visualstudio.microsoft.com/) with the **Desktop development with C++** workload
- [CMake 3.22+](https://cmake.org/download/) (tick "Add to PATH" during install)
- [Git for Windows](https://git-scm.com/download/win)
- Qt 6.8 — install via [aqtinstall](https://github.com/miurahr/aqtinstall):
  ```powershell
  pip install aqtinstall
  aqt install-qt windows desktop 6.8.1 win64_msvc2022_64 -O C:\Qt
  ```
- [vcpkg](https://github.com/microsoft/vcpkg) — follow the Quick Start guide; make sure `VCPKG_INSTALLATION_ROOT` is set

**Step 2 — Install libcurl via vcpkg**

```powershell
# Create a release-only static triplet (avoids multi-config linker errors)
$t = "$env:VCPKG_INSTALLATION_ROOT\triplets\community\x64-windows-static-release.cmake"
"set(VCPKG_TARGET_ARCHITECTURE x64)`nset(VCPKG_CRT_LINKAGE static)`nset(VCPKG_LIBRARY_LINKAGE static)`nset(VCPKG_BUILD_TYPE release)" | Set-Content $t

vcpkg install curl:x64-windows-static-release
```

**Step 3 — Build OBS from source (libobs + obs-frontend-api only)**

The OBS Windows release does not ship import `.lib` files. You must build a minimal subset of OBS from source to get `obs.lib` and `obs-frontend-api.lib`.

```powershell
git clone --depth 1 --branch 32.1.0 `
  https://github.com/obsproject/obs-studio.git C:\obs-src

# OBS auto-downloads its own dependencies (zlib, openssl, etc.) at configure time
cmake -B C:\obs-build -S C:\obs-src `
  -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_INSTALL_PREFIX="C:\obs-install" `
  -DENABLE_FRONTEND=OFF `
  -DENABLE_UI=OFF `
  -DENABLE_SCRIPTING=OFF `
  -DENABLE_PLUGINS=OFF `
  -DENABLE_HEVC=OFF

cmake --build C:\obs-build --config RelWithDebInfo `
  --target libobs obs-frontend-api --parallel

cmake --install C:\obs-build --config RelWithDebInfo --component Development
```

**Step 4 — Clone and build this plugin**

```powershell
git clone https://github.com/StephanBeutel/obs-twitch-ingest-benchmark.git
cd obs-twitch-ingest-benchmark

# Find the obs-deps path that OBS downloaded during configure
$obsDeps = Get-ChildItem "C:\obs-src\.deps" -Directory |
             Where-Object { $_.Name -like "obs-deps-*" } |
             Select-Object -First 1 -ExpandProperty FullName

cmake -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH="C:\obs-install;$obsDeps" `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_INSTALLATION_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static-release `
  -DCMAKE_CONFIGURATION_TYPES=RelWithDebInfo `
  -DQt6_DIR="C:\Qt\6.8.1\msvc2022_64\lib\cmake\Qt6"

cmake --build build --config RelWithDebInfo --parallel

# Copy DLL to OBS plugins folder
$dll = Get-ChildItem build -Recurse -Filter "obs-twitch-ingest-benchmark.dll" |
         Select-Object -First 1
Copy-Item $dll.FullName "C:\Program Files\obs-studio\obs-plugins\64bit\"
```

Restart OBS to pick up the plugin.

---

## CMake options

| Option | Default | Description |
|---|---|---|
| `OBS_APP` | — | Path to OBS.app bundle (macOS only) |
| `OBS_SOURCE_DIR` | — | Path to cloned OBS source tree (headers) |
| `OBS_ROOT` | — | OBS install prefix (Linux) |
| `Qt6_DIR` | auto | Path to Qt6 CMake config directory |

---

## Troubleshooting

| Symptom | Fix |
|---|---|
| Dock not visible after install | Enable it via **Docks → Twitch Ingest Benchmark** |
| Benchmark shows all servers as failed | TCP port 1935 outbound may be blocked by your firewall or router |
| Apply Best Server has no effect | OBS stream service must be set to **Twitch** (Settings → Stream → Service) |
| macOS: plugin fails to load | Verify OBS version matches — plugin is built against OBS 32.x |
| Windows: `Could not find OBS libraries` | Ensure the OBS mini-build completed and `C:\obs-install` exists |
| Windows: linker errors about missing `.lib` | Do not use the OBS Windows release zip — it contains no import libs; build from source as described above |

OBS plugin logs are written to **Help → Log Files → Current Log**. Search for `[obs-twitch-ingest-benchmark]` to filter plugin output.

---

## How scoring works

Each server is probed with multiple TCP connect attempts to port 1935. The composite score is:

```
score = 0.40 × latency_component
      + 0.25 × jitter_component
      + 0.25 × reliability_component
      + 0.10 × dns_component
```

Each component is normalised to `[0, 1]` across all servers in the run. The server with the highest score is marked as **Recommended**. Servers with zero successful TCP rounds receive a score of 0.

---

## License

MIT
