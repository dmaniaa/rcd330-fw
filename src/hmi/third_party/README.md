# Third-party inputs for `src/hmi`

The recreated `opt/hmi` must load on the device's own libraries, so it is
compiled against headers that match them exactly. Nothing fetched here is
committed: `tools/build_hmi.sh` downloads each archive once into
`build/hmi/third_party/downloads/`, checks its pinned sha256, and extracts the
headers into `build/hmi/third_party/`.

| Directory | Source | sha256 | Matches on device |
|---|---|---|---|
| `gcc46/c++/4.6` | Debian armel [`libstdc++6-4.6-dev_4.6.3-14_armel.deb`](http://archive.debian.org/debian/pool/main/g/gcc-4.6/libstdc++6-4.6-dev_4.6.3-14_armel.deb) (`usr/include/c++/4.6`) | `510b4ee79ba2923d5247c9225cef0626d980e67d828ea249be4ab6a2e90a9183` | `/lib/libstdc++.so.6.0.16` (GCC 4.6) |
| `qt474/include/QtCore`, `qt474/src/corelib` | [`qt-everywhere-opensource-src-4.7.4.tar.gz`](https://download.qt.io/archive/qt/4.7/qt-everywhere-opensource-src-4.7.4.tar.gz) (headers only; md5 `9831cf1dfa8d0689a06c2c54c5c65aaf` as published by Qt) | `97195ebce8a46f9929fb971d9ae58326d011c4d54425389e6e936514f540221e` | `/usr/local/qte4.7.4/lib/libQtCore.so.4` |
| `freetype248/include` | [`freetype-2.4.8.tar.bz2`](https://download.savannah.gnu.org/releases/freetype/freetype-old/freetype-2.4.8.tar.bz2) (`include/` only) | `a9eb7da3875fcb2f022a9c280c01b94ae45ac83d8102838c05dce1277948fb71` | `/usr/lib/libfreetype.so.6` (2.4.8: `FT_New_Library` stores version 2, 4, 8) |

In this Debian package the target-specific libstdc++ headers live in
`usr/include/c++/4.6/arm-linux-gnueabi/` (not the multiarch
`usr/include/arm-linux-gnueabi/c++/4.6/`), so the toolchain adds
`gcc46/c++/4.6/arm-linux-gnueabi` to the include path.

## Committed files

- `qt/qconfig.h`: stands in for the configure-generated `QtCore/qconfig.h` of
  the device's Qt Embedded 4.7.4 build (little-endian ARM, `Q_WS_QWS`,
  `QT_ARCH_ARM` so `qreal` is `float`). `src/hmi/CMakeLists.txt` copies it to
  `<build>/qt_config/QtCore/qconfig.h`, ahead of the Qt tree on the include
  path. It is proven by linking against the device `libQtCore.so.4` with
  unresolved symbols treated as errors: an unresolved Qt symbol means this
  file is wrong; fix it here, never stub the symbol.

The firmware update `.tar` does not carry `libQtCore.so.4`; the staged sysroot
gets it from `tools/prebuilt/libQtCore.so.4`, which is a Qt **4.8.2** ARM build
(`qreal = float`, exports every Qt symbol stock `opt/hmi` imports). That is
the library the build links against. Qt 4.8 is backward binary compatible
with 4.7, and compiling against the 4.7.4 headers keeps 4.8-only API out of
the binary.

Rule: the binary may import only Qt symbols that some stock 5624 binary
(`opt/hmi`, `opt/screenshot`, `opt/sysupdate`) already imports, since only
those are known to exist in the device's Qt 4.7.4; `test_qt_imports_subset_of_stock`
in `tools/tests/test_hmi_build.py` enforces this on every build.
