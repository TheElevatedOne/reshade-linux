# ReShade for Linux

![Static Badge](https://img.shields.io/badge/CLAUDE-VIBECODED?style=for-the-badge&logo=claude&label=VIBECODED&labelColor=%23333333&color=%23de7356) [![AUR Version](https://img.shields.io/aur/version/reshade-linux-bin?style=for-the-badge&logo=archlinux)](https://aur.archlinux.org/packages/reshade-linux-bin) [![AUR Version](https://img.shields.io/aur/version/lib32-reshade-linux-bin?style=for-the-badge&logo=archlinux)](https://aur.archlinux.org/packages/lib32-reshade-linux-bin) [![AUR Version](https://img.shields.io/aur/version/reshade-linux-git?style=for-the-badge&logo=archlinux)](https://aur.archlinux.org/packages/reshade-linux-git) [![AUR Version](https://img.shields.io/aur/version/lib32-reshade-linux-git?style=for-the-badge&logo=archlinux)](https://aur.archlinux.org/packages/lib32-reshade-linux-git)

<p align="center"><img src="res/main_icon.svg" alt="ReShade icon" width="128"></p>

A native Linux port of [ReShade], crosire's post-processing injector for games, that you turn on with `RESHADE=1`,
the same way you use `MANGOHUD=1`. It works for native Vulkan games and for Proton/Wine games, because DXVK and
VKD3D-Proton turn Direct3D 9–12 into Vulkan, and ReShade runs as a Vulkan layer.

> [!NOTE]
> This is an unofficial fork that only builds for Linux. For Windows, use the original at [crosire/reshade][ReShade].
> Report problems with this port [here][issues], not on the ReShade forum or Discord.

It installs these files:

| Part | File | Use it for |
|---|---|---|
| Vulkan layer | `/usr/lib/reshade/libreshade.so`, `/usr/lib32/reshade/libreshade.so` | the ReShade runtime, overlay and effect compiler for 64-bit and 32-bit games |
| Layer manifests | `/usr/share/vulkan/implicit_layer.d/reshade.x86_64.json`, `reshade.x86.json` | let the Vulkan loader load the layer when `RESHADE=1` is set |
| Launcher | `/usr/bin/reshade` | `reshade %command%`, and downloading the default effects |
| Desktop entry | `/usr/share/applications/me.reshade.ReShade.desktop` | names the global shortcuts in the desktop settings |

## How it works

ReShade is installed the same way the Windows setup tool installs it:

1. Run `reshade --download-shaders` once. It downloads the standard effects
   ([crosire/reshade-shaders], branch `slim`) to `~/.local/share/reshade/reshade-shaders`.
2. Start a game with `RESHADE=1`. On the first launch ReShade creates these files next to the game executable:

   ```text
   <game dir>/
   ├── ReShade.ini            created with the same defaults as the Windows setup tool
   ├── ReShade.log
   ├── ReShadePreset.ini      created when you pick effects in the overlay
   └── reshade-shaders/
       ├── Shaders/           a copy of the default effects
       └── Textures/
   ```

3. Press <kbd>Home</kbd> in the game to open the overlay, or bind a key to it in your desktop settings
   (see [Global shortcuts](#global-shortcuts)).

Effects you put in a game's `reshade-shaders` folder only apply to that game. Effects you add to
`~/.local/share/reshade/reshade-shaders` are copied into every game that you start for the first time afterwards.

For Proton games, the game directory is the folder of the Windows executable (e.g.
`steamapps/common/<Game>/bin/x64`), not the folder of the Wine loader. ReShade maps the Windows path to the Unix
path through the Wine prefix (`WINEPREFIX`, or `STEAM_COMPAT_DATA_PATH/pfx` for Proton).

## Requirements

- x86_64 Linux with a Vulkan 1.1+ driver (and the multilib 32-bit driver for 32-bit games)
- An X11 or Wayland session
- `libxcb`, `libxkbcommon`, `wayland`, `dbus`, `vulkan-icd-loader`
- For global shortcuts: `xdg-desktop-portal` 1.19 or newer with a backend that implements `GlobalShortcuts`
  (e.g. `xdg-desktop-portal-kde`, `xdg-desktop-portal-hyprland`)
- `git` and `curl` for `reshade --download-shaders`

## Installing

### Arch Linux (AUR)

| Package | Source |
|---|---|
| `reshade-linux-bin` | the 64-bit layer and launcher from the latest release |
| `lib32-reshade-linux-bin` | the 32-bit layer from the latest release, for 32-bit games |
| `reshade-linux-git` | builds the 64-bit layer and launcher from the latest commit of this repository |
| `lib32-reshade-linux-git` | builds the 32-bit layer from the latest commit, for 32-bit games (needs the multilib repository) |

The `lib32-` packages are separate packages that only contain the 32-bit layer. They need `reshade-linux-bin` or
`reshade-linux-git` for the launcher and desktop entry, so you can mix a `-bin` and a `-git` package.

```sh
yay -S reshade-linux-bin lib32-reshade-linux-bin
```

### Release tarball

Each [release][releases] has two tarballs, built on Arch Linux:

| File | Contents |
|---|---|
| `reshade-linux-<version>-x86_64.tar.gz` | `bin/reshade`, `lib/reshade/libreshade.so`, the layer manifest, desktop entry and icon |
| `reshade-linux-<version>-x86.tar.gz` | `lib32/reshade/libreshade.so` and its layer manifest |

The tarballs contain the `/usr` tree without the `usr/` prefix. The layer manifests point to
`/usr/lib/reshade/libreshade.so` and `/usr/lib32/reshade/libreshade.so`, so install the files to those paths:

```sh
tar xzf reshade-linux-6.8.0.1-x86_64.tar.gz
sudo cp -r reshade-linux-6.8.0.1-x86_64/{bin,lib,share} /usr/

tar xzf reshade-linux-6.8.0.1-x86.tar.gz
sudo cp -r reshade-linux-6.8.0.1-x86/{lib32,share} /usr/
```

On distributions that keep 32-bit libraries elsewhere (e.g. `/usr/lib/i386-linux-gnu`), build the 32-bit layer
yourself instead.

### Building

Build dependencies: `cmake`, `ninja`, `gcc`, `python`, `python-jinja`, `pkgconf`, `libx11`, `libxcb`, `libxkbcommon`,
`wayland`, `dbus`. For the 32-bit layer also `lib32-gcc-libs`, `lib32-libx11`, `lib32-libxcb`, `lib32-libxkbcommon`,
`lib32-wayland` and `lib32-dbus`.

```sh
git clone --recurse-submodules https://github.com/TheElevatedOne/reshade-linux.git
cd reshade-linux

# 64-bit layer, launcher and desktop entry
cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build-linux
sudo cmake --install build-linux

# 32-bit layer
cmake -S . -B build-linux32 -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
  -DCMAKE_TOOLCHAIN_FILE=linux/toolchain-i686.cmake -DCMAKE_INSTALL_LIBDIR=lib32 -DRESHADE_INSTALL_SHARED_FILES=OFF
cmake --build build-linux32
sudo cmake --install build-linux32
```

The build generates the Vulkan function loader with `glad` (from `deps/glad`) on the first build, which needs
`python-jinja`. The version number comes from `git describe`, so fetch the tags of the repository as well. Releases of this port
are tagged `<ReShade version>.<release>` (e.g. `6.8.0.1`), see [CHANGELOG.md](CHANGELOG.md).

| CMake option | Default | Description |
|---|---|---|
| `RESHADE_LAYER_NAME` | `VK_LAYER_reshade` | Vulkan layer name (the architecture is appended) |
| `RESHADE_DATA_DIR` | `<prefix>/share/reshade` | system-wide default effects (`reshade-shaders/`) and `Compatibility.ini` |
| `RESHADE_INSTALL_SHARED_FILES` | `ON` | install the launcher and desktop entry (turn off for the 32-bit build) |
| `RESHADE_VERSION_OVERRIDE` | from `git describe` | version to build as (`<major>.<minor>.<revision>[.<release>]`), for builds without the git tags |

## Usage

### Steam

Set the launch options of a game (native or Proton) to either of these:

```text
reshade %command%
RESHADE=1 %command%
```

### Other launchers and programs

```sh
reshade vkcube
RESHADE=1 lutris
```

### Launcher reference

```text
Usage: reshade [options] <command> [arguments...]

Options:
  --download-shaders [<git-url> [<branch>]]
                        Download effects into ~/.local/share/reshade/reshade-shaders, which is
                        copied into new game directories (default: crosire/reshade-shaders, branch
                        "slim"). Can be given several times to add more effect repositories.
  --list-shaders        List the effect files that are copied into new game directories.
  --update-shaders      Also copy effects that were added to the default effects since a game
                        was first launched (only copies missing files, never overwrites).
  --log                 Mirror the ReShade log to standard error.
  -h, --help            Show this help.
```

Examples:

```sh
# Standard effects plus the compatibility list of the Windows setup tool
reshade --download-shaders

# Add another effect repository to the defaults
reshade --download-shaders https://github.com/prod80/prod80-ReShade-Repository master

# Copy newly downloaded effects into a game that was already set up
reshade --update-shaders %command%
```

## Overlay input

ReShade runs inside the game as a Vulkan layer, so it receives input the way the window system lets a library in the
game process do it:

| Window system | How ReShade gets input | Input blocking while the overlay is open |
|---|---|---|
| X11 / XWayland (Proton's default) | XInput2 raw events on its own X11 connection, while the game window is focused | the pointer and keyboard are grabbed, so the game doesn't see the clicks |
| Native Wayland (`PROTON_ENABLE_WAYLAND=1`, SDL/GLFW games on Wayland) | its own `wl_seat` on the game's Wayland connection; the compositor sends the game's pointer and keyboard events to it as well | not possible; the game receives the same input |

### Global shortcuts

While a game runs, ReShade registers its actions with the desktop through the XDG global shortcuts portal, so you can bind
keys to them in the desktop settings. These work even when the game window isn't focused, and you can also send them to
the game from scripts.

| Shortcut ID | Action |
|---|---|
| `toggle-overlay` | open or close the overlay |
| `toggle-effects` | turn all effects on or off |
| `screenshot` | take a screenshot |
| `reload-effects` | recompile all effects |
| `next-preset` / `previous-preset` | switch to the next or previous preset in the preset folder |
| `toggle-fps` | show or hide the FPS counter |

The application ID is `me.reshade.ReShade`. ReShade doesn't suggest default keys, so nothing collides with your
existing shortcuts.

- **KDE Plasma**: on the first launch Plasma asks you to confirm the shortcuts. Assign keys afterwards in
  **System Settings → Keyboard → Shortcuts → ReShade**.
- **Hyprland**: bind a key to the `global` dispatcher. `hyprctl globalshortcuts` lists the registered IDs while a game
  runs.

  ```lua
  -- hyprland.lua
  hl.bind("SUPER + F12", function() hl.dispatch(hl.dsp.global("me.reshade.ReShade:toggle-overlay")) end)
  ```

  ```ini
  # hyprland.conf
  bind = SUPER, F12, global, me.reshade.ReShade:toggle-overlay
  ```

  To trigger an action from a script: `hyprctl dispatch global me.reshade.ReShade:screenshot`.

## Configuration

### Where ReShade looks

| What | Location |
|---|---|
| `ReShade.ini`, presets, log | next to the game executable. If that folder is read-only (e.g. `/usr/bin`), `~/.local/share/reshade/apps/<exe name>/` is used instead |
| Default effects | `~/.local/share/reshade/reshade-shaders`, then `/usr/share/reshade/reshade-shaders` |
| Template for new `ReShade.ini` files | `~/.local/share/reshade/ReShade.ini`, then `/usr/share/reshade/ReShade.ini` (optional) |
| Per-game depth settings | `Compatibility.ini` in the same two folders (downloaded by `--download-shaders`) |
| Effect cache | `~/.cache/reshade` |

All settings in `ReShade.ini` work the same as on Windows. Paths with backslashes (e.g.
`.\reshade-shaders\Shaders\**`) are accepted, so you can copy configurations and presets from a Windows install.
The `[INSTALL] BasePath` setting is supported as well.

### Environment variables

| Variable | Description |
|---|---|
| `RESHADE=1` | Enable the layer |
| `DISABLE_RESHADE=1` | Disable the layer again, e.g. for a child process |
| `RESHADE_BASE_PATH=<dir>` | Use `<dir>` instead of the game directory for `ReShade.ini`, presets and effects |
| `RESHADE_UPDATE_SHADERS=1` | Copy missing default effects into an existing game directory |
| `RESHADE_DISABLE_INPUT_GRAB=1` | Don't grab the mouse and keyboard while the overlay is in use (X11) |
| `RESHADE_DISABLE_GLOBAL_SHORTCUTS=1` | Don't register the global shortcuts with the desktop portal |
| `RESHADE_DISABLE_LOGGING=1` | Don't write `ReShade.log` (unless `[INSTALL] Logging=1` is set) |
| `RESHADE_LOG_STDERR=1` | Mirror the log to standard error |

## Add-ons

The built-in add-ons (generic depth buffer detection and effect runtime sync) are included. External add-ons are
loaded from `*.addon` and `*.addon64` files in the base path (or `[ADDON] AddonPath`) with `dlopen`, so they have to
be built for Linux. Windows add-on DLLs don't load. Build add-ons as shared objects with `-fPIC -fvisibility=hidden`
(plus `-Wno-changes-meaning` with GCC) and register them from a constructor function:

```cpp
#include <reshade.hpp>

__attribute__((constructor)) static void addon_load()
{
	reshade::register_addon(nullptr); // nullptr: the add-on's own module is looked up automatically
}

__attribute__((destructor)) static void addon_unload()
{
	reshade::unregister_addon(nullptr);
}
```

## Limitations

- **Vulkan only.** Native OpenGL games are not supported. Proton games are covered through DXVK and VKD3D-Proton.
  Games that Proton runs with WineD3D (`PROTON_USE_WINED3D=1`) use OpenGL and are not covered.
- **Input blocking.** On X11, games that read raw mouse motion may still turn the camera while the overlay is open.
  On native Wayland, the game always receives the input that goes to the overlay too.
- **Wayland scaling.** On native Wayland, pointer coordinates are assumed to match the swap chain size. Games that
  render at a different size than their logical window size (fractional scaling) get an offset overlay cursor.
- **FP16 swap chains.** ReShade treats `R16G16B16A16_SFLOAT` back buffers as scRGB HDR, also when the game uses the
  sRGB color space with them (e.g. `vkcube` on HDR-capable Wayland compositors). Effects written for SDR then look
  wrong in those games.
- **Global shortcuts** need a portal backend with `GlobalShortcuts` support and the installed
  `me.reshade.ReShade.desktop` file; without them, the log says that global shortcuts are not available.
- No gamepad navigation, VR (OpenVR/OpenXR), update check or translated UI.
- Screenshot sounds play through `pw-play`, `paplay` or `aplay`. **Open folder** uses the
  `org.freedesktop.FileManager1` D-Bus interface, or `xdg-open` as a fallback.

## Troubleshooting

- Check that the layer is found: `RESHADE=1 vulkaninfo --summary 2>&1 | grep -i reshade`.
- Read `ReShade.log` next to the game executable, or run with `reshade --log %command%` and read the Steam console
  output.
- If the game directory was set up before you downloaded effects, run it once with `reshade --update-shaders %command%`.

## Documentation

- [REFERENCE.md](REFERENCE.md): the ReShade FX shading language and the add-on API
- [examples/](examples/README.md): example add-ons from the original project (most are written for Windows)
- [aur/](aur/README.md): the AUR packaging

## License

ReShade is developed by crosire and licensed under the BSD 3-Clause license, see [LICENSE.md](LICENSE.md).
Some source files are also available under the MIT license, when stated at the top of those files.

[ReShade]: https://github.com/crosire/reshade
[issues]: https://github.com/TheElevatedOne/reshade-linux/issues
[releases]: https://github.com/TheElevatedOne/reshade-linux/releases
[crosire/reshade-shaders]: https://github.com/crosire/reshade-shaders
