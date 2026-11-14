# AUR packages

Each directory is one AUR package. The 32-bit layer has its own `lib32-` packages, as the
[Arch Linux 32-bit package guidelines][lib32] ask for, instead of being a split package of the 64-bit one.

| Directory | Builds from | Installs |
|---|---|---|
| `reshade-linux-bin/` | the `x86_64` release tarball | `/usr/lib/reshade/libreshade.so`, `/usr/share/vulkan/implicit_layer.d/reshade.x86_64.json`, the `reshade` launcher, the `me.reshade.ReShade` desktop entry and icon (needed for the global shortcuts portal) |
| `lib32-reshade-linux-bin/` | the `x86` release tarball | `/usr/lib32/reshade/libreshade.so`, `/usr/share/vulkan/implicit_layer.d/reshade.x86.json` for 32-bit games |
| `reshade-linux-git/` | the latest commit, 64-bit build | the same files as `reshade-linux-bin` |
| `lib32-reshade-linux-git/` | the latest commit, 32-bit build with `linux/toolchain-i686.cmake` (needs the multilib repository) | the same files as `lib32-reshade-linux-bin` |

The 64-bit packages provide `reshade-linux` and the 32-bit packages `lib32-reshade-linux`. The `lib32-` packages
depend on `reshade-linux`, which has the launcher and the desktop entry, so a `-bin` package can be combined with a
`-git` package of the other architecture. None of the packages ship any effects; users download them with
`reshade --download-shaders`.

## -bin packages

Releases are tagged `<ReShade version>.<release>` (e.g. `6.8.0.1`), and `pkgver` is the tag.
Pushing such a tag runs `.github/workflows/build.yml`, which builds both layers on Arch Linux and attaches
`reshade-linux-<version>-x86_64.tar.gz` and `reshade-linux-<version>-x86.tar.gz` to the GitHub release. The release
notes are the `## <version>` section of `CHANGELOG.md`. The tarballs are made by `linux/make-release-tarball.sh`.

After a release, set `pkgver` in both PKGBUILDs, then update the checksums and `.SRCINFO`:

```sh
for pkg in reshade-linux-bin lib32-reshade-linux-bin; do
  (cd "$pkg" && updpkgsums && makepkg --printsrcinfo > .SRCINFO)
done
```

## -git packages

The submodules in `deps/` are listed as separate git sources and pointed at in `prepare()`, so `makepkg` caches
them. `pkgver` comes from `git describe` (e.g. `6.8.0.1_r3_gabcdef12`), so the repository has to carry the release
tags.

Regenerate `.SRCINFO` after changing a PKGBUILD:

```sh
for pkg in reshade-linux-git lib32-reshade-linux-git; do
  (cd "$pkg" && makepkg --printsrcinfo > .SRCINFO)
done
```

## Test builds

`lib32-` packages depend on `reshade-linux`, so build and install the 64-bit package first, or skip the dependency
check with `makepkg -d` for a test build.

[lib32]: https://wiki.archlinux.org/title/32-bit_package_guidelines
