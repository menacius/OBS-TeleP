# OBS TeleP

Native C++/Qt OBS teleprompter/autocue plugin with a companion Android LAN remote.

OBS TeleP adds a dock inside OBS for editing and controlling scripts, and opens a separate fullscreen teleprompter output window on a selected display. The output is independent from the OBS preview/program canvas and does not affect scenes unless future source integration is added.

## Features

- Native OBS plugin, built in C++ with Qt.
- OBS dock for script editing, plain text import/export, Etherpad/plain URL loading, playback controls, display selection, styling, and remote status.
- Fullscreen teleprompter output on a selected monitor/projector.
- Horizontal mirror and vertical flip for autocue glass setups.
- Play/pause, stop, restart, jump to top, previous/next marker, speed control, and OBS frontend hotkeys.
- Font family, font size, line spacing, paragraph spacing, margins, text color, background color, horizontal alignment, and vertical alignment.
- Optional reading position indicator.
- Marker support with `#marker Name` lines.
- Persistent OBS profile/plugin settings, including script text, style, target display, fullscreen output state, remote token, output resolution, and audio trigger settings.
- Output resolution options: `100%`, `75%`, and `50%` internal render scale for lower CPU/GPU load on large displays.
- Audio input capture monitoring: pause when a selected audio input capture source becomes inactive, with optional auto-resume and configurable resume delay.
- Local TCP JSON API with token pairing.
- UDP discovery for LAN remotes.
- Android remote app with network scan, persistent server/token settings, dark mode, live play-state button color, playback controls, speed controls, size controls, marker navigation, and URL loading.

## Folder Layout

```text
.
├── android/                 Android remote app
├── data/locale/en-US.ini    OBS translation strings
├── docs/remote-protocol.md  TCP/UDP remote protocol
├── src/                     Native OBS plugin source
├── CMakeLists.txt           Native plugin build
└── build.ps1                Dependency bootstrap and build script
```

## Build

Build the native OBS plugin:

```powershell
.\build.ps1
```

Build from a clean native build directory:

```powershell
.\build.ps1 -Clean
```

Build the native plugin and Android remote:

```powershell
.\build.ps1 -BuildAndroid
```

The script checks for required tooling and downloads missing local dependencies where possible, including OBS Qt dependencies, JDK 17, Android command-line SDK tools, and Gradle.

Native plugin output:

```text
build/obs-telep/bin/64bit/obs-telep.dll
```

Android debug APK output:

```text
android/app/build/outputs/apk/debug/app-debug.apk
```

## Installing The Plugin

Copy the staged plugin folder contents into the OBS plugin directory:

```text
build/obs-telep/
```

Expected OBS layout:

```text
obs-studio/
├── obs-plugins/64bit/obs-telep.dll
└── data/obs-plugins/obs-telep/locale/en-US.ini
```

Depending on how OBS is installed, the target directory may be under `C:\Program Files\obs-studio` or a portable OBS folder.

## URL Loading

The dock and Android app can load scripts from plain text URLs.

For Etherpad-style URLs containing `/p/{pad}`, OBS TeleP automatically requests:

```text
/export/txt
```

The loader sends browser-like request headers and falls back to `curl.exe` on Windows if Qt networking is blocked by a server-side challenge or redirect behavior.

## Android Remote

The Android app connects to the plugin over the local network.

Default plugin ports:

- TCP control: `4457`
- UDP discovery: `4458`

The app can scan the LAN for available OBS TeleP instances. Discovery does not expose the pairing token; enter the token shown in the OBS dock.

The app stores these values between sessions:

- OBS host
- OBS port
- Pairing token
- Script URL

The Play/Pause button is state-driven:

- Green `Pause` means the teleprompter is currently scrolling.
- Red `Play` means the teleprompter is paused.

## Audio Source Auto-Pause

In the OBS dock, choose an audio input capture source under `Pause if audio source inactive`.

When the selected source is inactive, scrolling pauses. If `Resume when audio source becomes active` is enabled, scrolling resumes after the source has been active for the configured resume delay.

This is intended for microphone/audio input capture sources. The source list filters OBS sources by audio input capture source IDs.

## Performance Notes

For lower CPU/GPU usage:

- Use `Output resolution` at `75%` or `50%` for high-resolution teleprompter displays.
- Disable the reading position indicator if it is not needed.
- Keep font size and margins reasonable for the target display.

The output renderer avoids per-frame document relayout and clips text drawing to the visible script region.

## Remote Protocol

See [docs/remote-protocol.md](docs/remote-protocol.md).
