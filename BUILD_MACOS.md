# WSJT-64 on macOS (MacPorts) — Build & Run Guide

These instructions assume:

* macOS Catalina (10.15) **or newer**
* MacPorts already installed via https://www.macports.org/install.php

## 0) One-time shell setup (MacPorts PATH)

```bash
export PATH=/opt/local/bin:/opt/local/sbin:$PATH
```

(Optional, persistent)

```bash
echo 'export PATH=/opt/local/bin:/opt/local/sbin:$PATH' >> ~/.bash_profile
source ~/.bash_profile
```

---

## 1) Install build dependencies (MacPorts)

### Core tooling

```bash
sudo port selfupdate
sudo port install cmake ninja pkgconfig git
```

### Compilers

WSJT needs Fortran, and on macOS it’s easiest to use:

* **clang** for C/C++ (matches MacPorts Qt/Boost ABI)
* **gfortran** for Fortran (from GCC)

```bash
sudo port install clang-15 gcc13
```

> If you prefer gcc15, that’s fine too — just be consistent when bundling runtimes.

### Qt5 + modules WSJT actually needs

```bash
sudo port install qt5 qt5-qttools qt5-qtmultimedia qt5-qtserialport
```

### Boost + USB + audio dependencies

```bash
sudo port install boost libusb portaudio
```

### FFTW (single-precision + OpenMP)

WSJT wants **fftw3f** and **fftw3f_threads**, provided by `fftw-3-single`:

```bash
sudo port install libomp
sudo port install fftw-3-single +openmp
```

### Qt SQL SQLite driver (IMPORTANT)

Without this you’ll get: *“Database error: Driver not loaded”*

```bash
# Port name varies by MacPorts snapshot, so search then install:
port search qt5 | grep -i sqlite
sudo port install qt5-qtsqlite   # if available
# or install whichever “qt5 … sqlite” port your search shows
```

Verify the SQLite plugin exists:

```bash
ls -la /opt/local/libexec/qt5/plugins/sqldrivers/
```

---

## 2) Get the sources

```bash
mkdir -p ~/src ~/build ~/local
cd ~/src
git clone https://github.com/mkveksas/wsjt-64
```

---

## 3) Build & install Hamlib (local prefix)

If you already have your Hamlib build process, keep it. Example “local prefix” approach:

```bash
cd ~/src
git clone https://github.com/Hamlib/Hamlib.git hamlib
cd hamlib
./bootstrap

mkdir -p ~/build/hamlib/release
cd ~/build/hamlib/release

cmake -S ~/src/hamlib -B . -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$HOME/local/hamlib"

cmake --build . -- -j"$(sysctl -n hw.ncpu)"
cmake --install .
```

Set convenience variable for later:

```bash
HL="$HOME/local/hamlib"
```

---

## 4) Configure & build WSJT-64

### Choose compilers

Use MacPorts clang for C/C++ and MacPorts gfortran for Fortran.

Find actual paths (versions may differ):

```bash
ls /opt/local/bin/clang-mp-* /opt/local/bin/clang++-mp-* /opt/local/bin/gfortran-mp-*
```

Example (adjust numbers as installed):

* `/opt/local/bin/clang-mp-15`
* `/opt/local/bin/clang++-mp-15`
* `/opt/local/bin/gfortran-mp-13`

### Configure

```bash
mkdir -p ~/build/wsjt-64/release
cd ~/build/wsjt-64/release

QT5=/opt/local/libexec/qt5

cmake -S ~/src/wsjt-64 -B . -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=/opt/local/bin/clang-mp-15 \
  -DCMAKE_CXX_COMPILER=/opt/local/bin/clang++-mp-15 \
  -DCMAKE_Fortran_COMPILER=/opt/local/bin/gfortran-mp-13 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15 \
  -DCMAKE_PREFIX_PATH="$HL;$QT5;/opt/local" \
  -DCMAKE_C_FLAGS="-Wno-error=deprecated-declarations" \
  -DCMAKE_CXX_FLAGS="-Wno-error=deprecated-declarations" \
  -DWSJT_GENERATE_DOCS=OFF \
  -DWSJT_SKIP_MANPAGES=ON \
  -DWSJT_ENABLE_OMNIRIG=OFF \
  -DCMAKE_INSTALL_PREFIX="$HOME/local/wsjt-64"
```

Build:

```bash
cmake --build . -- -j"$(sysctl -n hw.ncpu)"
```

---

## 5) Create a runnable `.app` bundle

At this point you typically have `wsjtx.app` in the build directory:

```bash
ls -la wsjtx.app
```

Copy it to a sane place:

```bash
mkdir -p ~/Applications
cp -R wsjtx.app ~/Applications/
```

---

## 6) Bundle Qt frameworks/plugins with macdeployqt

MacPorts `macdeployqt` is here:

```bash
/opt/local/libexec/qt5/bin/macdeployqt ~/Applications/wsjtx.app
```

### If `macdeployqt` errors on `@rpath/libgfortran…`

Add rpaths to the main binary before retrying:

```bash
# Pick a GCC runtime directory (keep consistent with your gfortran)
GCCLIBDIR=/opt/local/lib/gcc13

install_name_tool -add_rpath "$GCCLIBDIR" ~/Applications/wsjtx.app/Contents/MacOS/wsjtx
install_name_tool -add_rpath "$HL/lib"    ~/Applications/wsjtx.app/Contents/MacOS/wsjtx

/opt/local/libexec/qt5/bin/macdeployqt ~/Applications/wsjtx.app
```

### Ensure qt.conf points plugins correctly

```bash
cat > ~/Applications/wsjtx.app/Contents/Resources/qt.conf <<'EOF'
[Paths]
Plugins = PlugIns
EOF
```

---

## 7) Ensure SQLite Qt driver is inside the app

If `macdeployqt` didn’t include it, copy it:

```bash
mkdir -p ~/Applications/wsjtx.app/Contents/PlugIns/sqldrivers
cp -v /opt/local/libexec/qt5/plugins/sqldrivers/libqsqlite*.dylib \
      ~/Applications/wsjtx.app/Contents/PlugIns/sqldrivers/
```

If the plugin exists but “Driver not loaded”, it usually needs MacPorts sqlite bundled too:

```bash
mkdir -p ~/Applications/wsjtx.app/Contents/Frameworks
cp -vn /opt/local/lib/libsqlite3.0.dylib ~/Applications/wsjtx.app/Contents/Frameworks/

install_name_tool -id \
  @executable_path/../Frameworks/libsqlite3.0.dylib \
  ~/Applications/wsjtx.app/Contents/Frameworks/libsqlite3.0.dylib
```

---

## 8) Fix common missing dylibs inside the `.app` (MacPorts Qt deps)

If launching gives “Library not loaded …”, copy the missing dylib into:
`~/Applications/wsjtx.app/Contents/Frameworks/`

Typical offenders (what we hit):

```bash
cp -vn /opt/local/lib/libdouble-conversion*.dylib ~/Applications/wsjtx.app/Contents/Frameworks/ 2>/dev/null || true
cp -vn /opt/local/lib/libpcre2-16*.dylib          ~/Applications/wsjtx.app/Contents/Frameworks/ 2>/dev/null || true
cp -vn /opt/local/lib/libzstd*.dylib              ~/Applications/wsjtx.app/Contents/Frameworks/ 2>/dev/null || true
cp -vn /opt/local/lib/libgthread-2.0.0.dylib      ~/Applications/wsjtx.app/Contents/Frameworks/ 2>/dev/null || true
cp -vn /opt/local/lib/libharfbuzz.0.dylib         ~/Applications/wsjtx.app/Contents/Frameworks/ 2>/dev/null || true
```

If a dependency is still referenced as `/opt/local/lib/...`, rewrite it to the bundled path using `install_name_tool -change`. (Same pattern you used earlier.)

---

## 9) Fix decoder runtime (JT9/libgomp) & ensure decoders exist in the `.app`

### If JT9 fails: `@rpath/libgomp.1.dylib not found`

Bundle GCC OpenMP runtime:

```bash
cp -vn /opt/local/lib/gcc13/libgomp.1.dylib ~/Applications/wsjtx.app/Contents/Frameworks/
install_name_tool -id \
  @executable_path/../Frameworks/libgomp.1.dylib \
  ~/Applications/wsjtx.app/Contents/Frameworks/libgomp.1.dylib
```

### Ensure decoder executables are actually inside the `.app`

Sometimes the `.app` doesn’t contain `jt9`, `ft8code`, etc. If WSJT complains about JT9/decoders:

Copy them from your build dir into `Contents/MacOS`:

```bash
cd ~/build/wsjt-64/release
cp -v jt9 jt65 ft8code jt4code q65code wsprd wsprcode \
  ~/Applications/wsjtx.app/Contents/MacOS/ 2>/dev/null || true
```

Patch any decoder that references `@rpath/libgomp.1.dylib`:

```bash
for f in ~/Applications/wsjtx.app/Contents/MacOS/*; do
  otool -L "$f" 2>/dev/null | grep -q '@rpath/libgomp.1.dylib' || continue
  install_name_tool -add_rpath @executable_path/../Frameworks "$f" 2>/dev/null || true
  install_name_tool -change @rpath/libgomp.1.dylib @executable_path/../Frameworks/libgomp.1.dylib "$f" 2>/dev/null || true
done
```

---

## 10) Catalina shared-memory fix (the “Shared memory error”)

On Catalina you may need to raise SysV shared memory limits (WSJT uses IPC).

Check:

```bash
sysctl kern.sysv.shmmax kern.sysv.shmall kern.sysv.shmseg
```

If values are tiny (like 4MB total), temporarily bump:

```bash
sudo sysctl -w kern.sysv.shmmax=67108864
sudo sysctl -w kern.sysv.shmall=16384
sudo sysctl -w kern.sysv.shmseg=32
```

### Make persistent across reboot (optional)

Create a LaunchDaemon that applies the sysctl settings at boot (you can reuse the exact plist you already tested).

---

## 11) Run (Terminal / Finder)

Terminal:

```bash
~/Applications/wsjtx.app/Contents/MacOS/wsjtx
```

Finder:

```bash
open -a ~/Applications/wsjtx.app
```

---

# Notes for newer macOS versions (Big Sur → Sonoma)

* **SIP / hardened runtime**: Finder-launched apps may ignore some dynamic loader environment variables. Prefer bundling dylibs inside the `.app` (Frameworks + PlugIns) rather than relying on `DYLD_LIBRARY_PATH`.
* **Qt5 availability**: Qt5 is increasingly “legacy” on newer macOS; MacPorts still supports it, but you may see more missing dylib/plugin chasing. The bundling steps above remain valid.
* **Deployment target**: for newer macOS you can set:

  * `-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0` (or whatever you target)
  * but on Catalina keep it `10.15`.

---

# Quick debugging commands

### See what dylibs a binary needs

```bash
otool -L ~/Applications/wsjtx.app/Contents/MacOS/wsjtx
```

### Trace Qt plugin loading (very noisy but useful)

```bash
QT_DEBUG_PLUGINS=1 ~/Applications/wsjtx.app/Contents/MacOS/wsjtx 2>&1 | tee ~/wsjt_qt_debug.log
```

### Identify missing dylibs at runtime

If it says “Library not loaded …X…”, copy `X` into `Contents/Frameworks`, then (if needed) use `install_name_tool -change` to point references to `@executable_path/../Frameworks/X`.

---
