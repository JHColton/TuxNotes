# TuxNotes

An accurate clone of macOS Stickies, built for **KDE Plasma on Wayland**.

Frameless, multi-colored sticky notes that live on your desktop — with the
little details that make Stickies feel like Stickies: painted title strips,
roll-up, the zoom triangle, and notes that reappear exactly where you left
them.

![TuxNotes](icon.png)

## Features

- **Faithful chrome** — close box, drag strip, roll-up button (dog-eared page),
  zoom triangle, corner resize grip, all painted to match macOS Stickies
- **Six classic colors** (`Ctrl+1`–`6`) with the original palette
- **Rich text** — bold/italic/underline, fonts, sizes, alignment
- **Roll up** (`Ctrl+M`) — collapse a note to a title strip showing its first line
- **Zoom** — fill the work area (panel-aware), click again to restore
- **Float on Top** (`Ctrl+Alt+F`) and **Translucent** (`Ctrl+Alt+T`)
- **Exact position restore** — notes reappear pixel-identical across restarts,
  even on Wayland
- **Autosave** — continuous, crash-safe; positions tracked live while you drag
- **Single instance** — launching again opens a new note

## Install

### Fedora (RPM)

Build and install locally:

```bash
sudo dnf install rpm-build          # once
./packaging/build-rpm.sh
sudo dnf install ~/rpmbuild/RPMS/x86_64/tuxnotes-*.rpm
```

### AppImage (portable, any modern distro)

Download `tuxnotes-<version>-x86_64.AppImage`, make it executable, run it:

```bash
chmod +x tuxnotes-*-x86_64.AppImage
./tuxnotes-*-x86_64.AppImage
```

Built against glibc 2.39 — works on Fedora 40+, Ubuntu 24.04+, Debian 13+.
Runs natively on Wayland (falls back to X11 elsewhere).

> **Note for AppImage users:** the taskbar icon resolves best when a desktop
> entry exists (install the RPM, or use AppImageLauncher).

## Building from source

Requirements: Qt 6.6+ (Gui, Widgets, DBus), CMake 3.21+, a C++20 compiler.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/tuxnotes
```

### Packaging

```bash
./packaging/build-rpm.sh       # RPM (requires rpm-build)
./packaging/build-appimage.sh  # portable AppImage (requires podman or docker)
```

## How it works on Wayland

Wayland deliberately prevents clients from positioning their own windows or
setting keep-above. TuxNotes bridges this with a tiny KWin scripting layer:

- a persistent watcher script correlates each note window by its compositor
  `internalId` and streams geometry changes back over D-Bus (crash-safe
  persistence),
- one-shot apply scripts place windows exactly, set keep-above/opacity, and
  query the compositor's panel-aware maximize area.

On X11 the same features use plain window-manager calls instead.

## Keyboard shortcuts

| Action | Keys |
|---|---|
| New note | `Ctrl+N` |
| Colors | `Ctrl+1` … `Ctrl+6` |
| Bold / Italic / Underline | `Ctrl+B` / `Ctrl+I` / `Ctrl+U` |
| Roll up / expand | `Ctrl+M` or double-click title bar |
| Float on Top | `Ctrl+Alt+F` |
| Translucent | `Ctrl+Alt+T` |
| Delete note | `Ctrl+W` or the close box |

## License

[GPL-3.0](LICENSE)
