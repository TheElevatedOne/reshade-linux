# AUR packages

| Directory | Packages | Builds from |
|---|---|---|
| `reshade-linux-bin/` | `reshade-linux-bin`, `lib32-reshade-linux-bin` | the release tarballs of https://github.com/TheElevatedOne/reshade-linux |
| `reshade-linux-git/` | `reshade-linux-git`, `lib32-reshade-linux-git` | the latest commit of https://github.com/TheElevatedOne/reshade-linux |

Both are split packages (`pkgbase`) with a 64-bit and a 32-bit package:

| Package | Installs |
|---|---|
| `reshade-linux-bin`, `reshade-linux-git` | `/usr/lib/reshade/libreshade.so`, `/usr/share/vulkan/implicit_layer.d/reshade.x86_64.json`, the `reshade` launcher, the `me.reshade.ReShade` desktop entry and icon (needed for the global shortcuts portal) |
| `lib32-reshade-linux-bin`, `lib32-reshade-linux-git` | `/usr/lib32/reshade/libreshade.so`, `/usr/share/vulkan/implicit_layer.d/reshade.x86.json` for 32-bit games |

The 64-bit packages provide `reshade-linux` and the 32-bit packages `lib32-reshade-linux`, so a `-bin` package can be
combined with a `-git` package of the other architecture. None of the packages ship any effects; users download
them with `reshade --download-shaders`.

## reshade-linux-bin

Releases are tagged `v<ReShade version>.<release>` (e.g. `v6.8.0.1`), and `pkgver` is the tag without the `v`.
Pushing such a tag runs `.github/workflows/build.yml`, which builds both layers on Arch Linux and attaches
`reshade-linux-<version>-x86_64.tar.gz` and `reshade-linux-<version>-x86.tar.gz` to the GitHub release. The release
notes are the `## <version>` section of `CHANGELOG.md`. The tarballs are made by `linux/make-release-tarball.sh`.

After a release, update the checksums and `.SRCINFO`:

```sh
cd reshade-linux-bin
updpkgsums
makepkg --printsrcinfo > .SRCINFO
```

## reshade-linux-git

The 32-bit build uses `linux/toolchain-i686.cmake` and needs the multilib repository. The submodules in `deps/` are
listed as separate git sources and pointed at in `prepare()`, so `makepkg` caches them. `pkgver` comes from
`git describe` (e.g. `6.8.0.1_r3_gabcdef12`), so the repository has to carry the `v*` tags.

Regenerate `.SRCINFO` after changing the PKGBUILD:

```sh
cd reshade-linux-git
makepkg --printsrcinfo > .SRCINFO
```
