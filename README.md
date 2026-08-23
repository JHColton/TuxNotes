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

## Download

Pre-built binaries are available on the
[Releases page](https://github.com/JHColton/TuxNotes/releases):

- **AppImage** — portable, runs on any modern distro (glibc 2.39+:
  Fedora 40+, Ubuntu 24.04+, Debian 13+). No installation required:

  ```bash
  chmod +x tuxnotes-*-x86_64.AppImage
  ./tuxnotes-*-x86_64.AppImage
  ```

- **RPM** (Fedora) — installs the app, launcher entry and icon:

  ```bash
  sudo dnf install ./tuxnotes-*.rpm
  ```

> **Tip for AppImage users:** the taskbar icon resolves best when a desktop
> entry exists — install the RPM, or use
> [AppImageLauncher](https://github.com/TheAssassin/AppImageLauncher).

## Build from source

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

## AI-assisted development

TuxNotes was built with substantial AI assistance: the implementation was
written by an AI coding assistant working iteratively under human direction.
The source is deliberately small (~2,300 lines) and meant to be readable
Architecture and product decisions, Wayland/KWin research, testing, and every
merge were done by the author.

The TuxNotes icon is AI-generated and depicts Tux, the Linux
penguin originally drawn by Larry Ewing.


## License

[GPL-3.0](LICENSE)
