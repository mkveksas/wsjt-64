# WSJT-64 Build Guide — Windows 11 (MSYS2 + MINGW64)
## Up to date as of 2026-02-22

This guide builds WSJT-64 entirely inside MSYS2 using:

✅ MSYS2 MINGW64 toolchain  
✅ Qt from MSYS2 (no Qt account needed)  
✅ Hamlib built from source  
✅ No HamlibSDK  
✅ No admin privileges  
✅ No Program Files install

---

## 📥 1. Install MSYS2

Download and install MSYS2:

[https://www.msys2.org](https://www.msys2.org)

Use default path:

```
C:\msys64
```

Launch:

👉 **MSYS2 MINGW64** (blue terminal)

---

## 🔄 2. Update MSYS2

```bash
pacman -Syu
```

Close terminal if prompted, reopen **MSYS2 MINGW64**, then:

```bash
pacman -Syu
```

---

## 📦 3. Install Required Packages

```bash
pacman -S --needed \
  git \
  mingw-w64-x86_64-toolchain \
  mingw-w64-x86_64-gcc-fortran \
  mingw-w64-x86_64-cmake \
  mingw-w64-x86_64-ninja \
  mingw-w64-x86_64-pkgconf \
  mingw-w64-x86_64-qt5-base \
  mingw-w64-x86_64-qt5-multimedia \
  mingw-w64-x86_64-qt5-serialport \
  mingw-w64-x86_64-qt5-tools \
  mingw-w64-x86_64-qt5-activeqt \
  mingw-w64-x86_64-fftw \
  mingw-w64-x86_64-boost \
  mingw-w64-x86_64-libusb \
  mingw-w64-x86_64-portaudio \
  autoconf automake libtool make
```

---

# 📡 4. Build Hamlib from Source

Next, we will install Hamlib into (go to "Clone Hamlib" step):

```
~/local/hamlib/mingw64/release
```

### Clone Hamlib

```bash
mkdir -p ~/src ~/build ~/local
cd ~/src
git clone https://github.com/Hamlib/Hamlib hamlib
cd hamlib
./bootstrap
```

### Configure & Build

```bash
mkdir -p ~/build/hamlib/release
cd ~/build/hamlib/release

~/src/hamlib/configure \
  --prefix=$HOME/local/hamlib/mingw64/release \
  --enable-shared --disable-static \
  --without-cxx-binding \
  --disable-winradio

make -j$(nproc)
make install-strip
```

Verify:

```bash
ls ~/local/hamlib/mingw64/release/bin
```

You should see:

```
rigctl.exe
libhamlib-*.dll
```

`libhamlib-*.dll` means it might be `libhamlib-5.dll` or `libhamlib-2.dll`, etc.

---

# 📥 5. Clone WSJT-64

```bash
cd ~/src
git clone https://github.com/mkveksas/wsjt-64
```

---

# ⚙️ 6. Configure WSJT-64 Build

```bash
mkdir -p ~/build/wsjt-64/release
cd ~/build/wsjt-64/release

HL=$HOME/local/hamlib/mingw64/release

cmake -S ~/src/wsjt-64 -B . -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_FLAGS="-Wno-error=deprecated-declarations" \
  -DCMAKE_CXX_FLAGS="-Wno-error=deprecated-declarations" \
  -DHamlib_INCLUDE_DIR:PATH="$HL/include" \
  -DHamlib_LIBRARY:FILEPATH="$HL/lib/libhamlib.dll.a" \
  -DCMAKE_PREFIX_PATH:PATH="/mingw64;$HL" \
  -DRIGCTL_EXE:FILEPATH="$HL/bin/rigctl.exe" \
  -DWSJT_GENERATE_DOCS=OFF \
  -DWSJT_ENABLE_OMNIRIG=OFF \
  -DCMAKE_INSTALL_PREFIX:PATH="$HOME/local/wsjt-64"
```

---

# 🛠️ 7. Build WSJT-64

```bash
cmake --build . -- -j"$(nproc)"
```

# ▶️ 8. Run WSJT-64

From MINGW64 terminal:

```bash
export PATH=/mingw64/bin:$HOME/local/hamlib/mingw64/release/bin:$PATH
./wsjtx.exe
```

WSJT-64 should now start normally.

---

## ✅ What This Setup Achieves

✔ Builds clean on fresh Windows 11  
✔ No proprietary Qt installer  
✔ No HamlibSDK  
✔ No admin access  
✔ Fully reproducible  
✔ Works entirely inside MSYS2  

