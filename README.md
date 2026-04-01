# obs-twitch-ingest-benchmark

An OBS Studio plugin that benchmarks all Twitch ingest servers and automatically applies the fastest one before you start streaming.

## Features

- Fetches the live Twitch ingest server list from the Twitch API
- Measures DNS resolution latency and TCP connect time across multiple rounds per server
- Scores servers by a weighted combination of latency, jitter, reliability, and DNS performance
- Displays results in a sortable table inside an OBS dock
- **Auto-benchmark mode**: intercepts the OBS "Start Streaming" button and runs the benchmark first, applies the best server, then starts the stream — no extra clicks required
- EU-only filter for streamers who want to restrict results to European servers
- Settings are persisted in OBS's global config

## Requirements

| | Version |
|---|---|
| OBS Studio | 30.0 or later |
| Qt | 6.4 or later |
| CMake | 3.22 or later |
| Compiler | C++17 (Xcode 14+ / MSVC 2022 / GCC 12+) |

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

After installation, the **Twitch Ingest Benchmark** dock appears under **Docks** in the OBS menu bar.

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

**Prerequisites**

```sh
xcode-select --install
brew install cmake qt@6
```

**Steps**

```sh
# 1. Download OBS.app (provides libobs.framework and Qt frameworks)
#    Install from https://obsproject.com — place at /Applications/OBS.app

# 2. Clone OBS source headers (needed for #include <obs-module.h> etc.)
git clone --depth 1 --branch 32.1.0 \
  https://github.com/obsproject/obs-studio.git /tmp/obs-headers

# 3. Clone this repo
git clone https://github.com/<you>/obs-twitch-ingest-benchmark.git
cd obs-twitch-ingest-benchmark

# 4. Configure
cmake -B build \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DOBS_APP=/Applications/OBS.app \
  -DOBS_SOURCE_DIR=/tmp/obs-headers \
  -DQt6_DIR="$(brew --prefix qt@6)/lib/cmake/Qt6"

# 5. Build
cmake --build build --config RelWithDebInfo --parallel

# 6. Install
cmake --install build
# Installs to ~/Library/Application Support/obs-studio/plugins/
```

Restart OBS to pick up the freshly built plugin.

### Windows (x64)

Requires **Visual Studio 2022** and **Qt 6** installed via [aqtinstall](https://github.com/miurahr/aqtinstall) or the Qt Maintenance Tool.

```powershell
# 1. Clone OBS source headers
git clone --depth 1 --branch 32.1.0 `
  https://github.com/obsproject/obs-studio.git C:\obs-headers

# 2. Download and extract the OBS Windows runtime
$ver = "32.1.0"
$url = "https://github.com/obsproject/obs-studio/releases/download/$ver/OBS-Studio-$ver-Windows-x64.zip"
Invoke-WebRequest -Uri $url -OutFile obs-windows.zip
Expand-Archive obs-windows.zip -DestinationPath C:\obs-windows

# 3. Install libcurl (vcpkg)
vcpkg install curl:x64-windows-static

# 4. Clone this repo
git clone https://github.com/<you>/obs-twitch-ingest-benchmark.git
cd obs-twitch-ingest-benchmark

# 5. Configure (adjust Qt path as needed)
cmake -B build -G "Visual Studio 17 2022" -A x64 `
  -DOBS_SOURCE_DIR="C:\obs-headers" `
  -DCMAKE_PREFIX_PATH="C:\Qt\6.8.1\msvc2022_64" `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_INSTALLATION_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static

# 6. Build
cmake --build build --config RelWithDebInfo --parallel

# 7. Copy the plugin DLL
$dll = Get-ChildItem build -Recurse -Filter "obs-twitch-ingest-benchmark.dll" |
         Select-Object -First 1
Copy-Item $dll.FullName "C:\Program Files\obs-studio\obs-plugins\64bit\"
```

---

## CMake options

| Option | Default | Description |
|---|---|---|
| `OBS_APP` | — | Path to OBS.app bundle (macOS only) |
| `OBS_SOURCE_DIR` | — | Path to cloned OBS source tree (headers) |
| `OBS_ROOT` | — | OBS install prefix (Linux / Windows SDK) |
| `Qt6_DIR` | auto | Path to Qt6 cmake config directory |

---

## Troubleshooting

| Symptom | Fix |
|---|---|
| Dock not visible after install | Enable it via **Docks → Twitch Ingest Benchmark** |
| `Could not find OBS libraries` | Pass `-DOBS_APP=` or `-DOBS_ROOT=` to CMake |
| `Qt6 not found` | Pass `-DQt6_DIR=/path/to/Qt/6.x/platform/lib/cmake/Qt6` |
| Benchmark shows all servers as failed | Check that TCP port 1935 outbound is not blocked by your firewall |
| Apply Best Server has no effect | Make sure OBS stream service is configured as **Twitch** (not Custom RTMP) |
| Plugin crashes OBS on load | Verify the plugin was built against the same OBS major version that is installed |

OBS plugin logs are written to the OBS log file (**Help → Log Files → Current Log**). Search for `[obs-twitch-ingest-benchmark]` to filter plugin output.

---

## How scoring works

Each server is probed with multiple TCP connect attempts to port 1935. The composite score is computed as:

```
score = 0.40 × latency_component
      + 0.25 × jitter_component
      + 0.25 × reliability_component
      + 0.10 × dns_component
```

Each component is normalised to `[0, 1]` across all servers in the run. The server with the highest score is marked as **Recommended**. Servers with zero successful TCP rounds receive a score of 0.

## License

MIT
