# Changelog

Releases are tagged `<ReShade version>.<release>`: `6.8.0.1` is the first release of this port based on ReShade 6.8.0.

## 6.8.0.1

First release of the Linux port, based on ReShade 6.8.0.

- Added: ReShade as an implicit Vulkan layer, turned on with `RESHADE=1` (like `MANGOHUD=1`), for native Vulkan
  games and Proton games through DXVK and VKD3D-Proton. 64-bit and 32-bit layers.
- Added: on the first launch of a game, `ReShade.ini` and a copy of the default effects are created next to the
  game executable (the Windows executable for Proton games), the same way the Windows setup tool installs ReShade.
- Added: `reshade` launcher: `reshade %command%` in Steam launch options, `reshade --download-shaders` downloads the
  standard effects and the depth buffer compatibility list.
- Added: overlay input on X11/XWayland (XInput2, the game doesn't receive the input while the overlay uses it) and on
  native Wayland (the game still receives the input).
- Added: global shortcuts through the XDG desktop portal (`me.reshade.ReShade:toggle-overlay`, `toggle-effects`,
  `screenshot`, `reload-effects`, `next-preset`, `previous-preset`, `toggle-fps`), bindable in the KDE shortcut
  settings or with Hyprland's `global` dispatcher.
- Added: add-ons built for Linux (`*.addon`, `*.addon64` shared objects).
- Removed: the Windows build, Direct3D/OpenGL hooks, VR, the update check and translations.
