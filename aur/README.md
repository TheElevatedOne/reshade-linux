# AUR packages

| Directory | Packages | Builds from |
|---|---|---|
| `reshade-linux-git/` | `reshade-linux-git`, `lib32-reshade-linux-git` | the latest commit of https://github.com/TheElevatedOne/reshade-linux |

`reshade-linux-git` is a split package (`pkgbase`) that builds the layer twice:

| Package | Installs |
|---|---|
| `reshade-linux-git` | `/usr/lib/reshade/libreshade.so`, `/usr/share/vulkan/implicit_layer.d/reshade.x86_64.json`, the `reshade` launcher, the `me.reshade.ReShade` desktop entry and icon (needed for the global shortcuts portal) |
| `lib32-reshade-linux-git` | `/usr/lib32/reshade/libreshade.so`, `/usr/share/vulkan/implicit_layer.d/reshade.x86.json` for 32-bit games |

The 32-bit build uses `linux/toolchain-i686.cmake` and needs the multilib repository. Neither package ships any effects;
users download them with `reshade --download-shaders`.

The submodules in `deps/` are listed as separate git sources and pointed at in `prepare()`, so `makepkg` caches
them. `pkgver` comes from `git describe` of the upstream tags (e.g. `6.8.0_r28_g7bf9de8b`), so the
repository has to carry the `v*` tags of crosire/reshade.

Regenerate `.SRCINFO` after changing the PKGBUILD:

```sh
cd reshade-linux-git
makepkg --printsrcinfo > .SRCINFO
```
