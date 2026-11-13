#!/bin/sh
# Build a release tarball: dist/reshade-linux-<version>-<arch>.tar.gz
# Usage: linux/make-release-tarball.sh <build-dir> <version> <arch>
#   <arch> is x86_64 (64-bit layer, launcher, desktop entry) or x86 (32-bit layer only).
# The build directory must be configured with -DCMAKE_INSTALL_PREFIX=/usr (and -DCMAKE_INSTALL_LIBDIR=lib or lib32) and built.
# The tarball holds the installed tree without the "usr/" prefix, plus README.md, CHANGELOG.md and LICENSE.md.
set -eu
build=$1
version=$2
arch=$3
root=$(cd "$(dirname "$0")/.." && pwd)
name="reshade-linux-$version-$arch"
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT

DESTDIR="$stage/install" cmake --install "$build" --strip >/dev/null
case "$arch" in
	x86_64)
		test -f "$stage/install/usr/lib/reshade/libreshade.so"
		test -f "$stage/install/usr/share/vulkan/implicit_layer.d/reshade.x86_64.json"
		test -x "$stage/install/usr/bin/reshade"
		;;
	x86)
		test -f "$stage/install/usr/lib32/reshade/libreshade.so"
		test -f "$stage/install/usr/share/vulkan/implicit_layer.d/reshade.x86.json"
		;;
	*)
		echo "make-release-tarball.sh: unknown architecture '$arch' (expected x86_64 or x86)" >&2
		exit 1
		;;
esac

mkdir -p "$stage/$name"
cp -a "$stage/install/usr/." "$stage/$name/"
cp "$root/README.md" "$root/CHANGELOG.md" "$root/LICENSE.md" "$stage/$name/"
mkdir -p "$root/dist"
tar --owner=0 --group=0 -C "$stage" -czf "$root/dist/$name.tar.gz" "$name"
(cd "$root/dist" && sha256sum "$name.tar.gz" > "$name.tar.gz.sha256")
echo "$root/dist/$name.tar.gz"
