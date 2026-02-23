# WSJT-64 Build Guide — Ubuntu 24.04 LTS and Ubuntu 25.10
## Exact same instructions both for 24.04 and 25.10
## Up to date as of 2026-02-23

---

## 1️⃣ Install Build Dependencies

```bash
sudo apt update

sudo apt install -y \
  build-essential \
  gfortran \
  git \
  cmake \
  ninja-build \
  pkg-config \
  autoconf automake libtool \
  qtbase5-dev \
  qttools5-dev \
  qttools5-dev-tools \
  libqt5serialport5-dev \
  qtmultimedia5-dev \
  libboost-all-dev \
  libfftw3-dev \
  libusb-1.0-0-dev \
  portaudio19-dev \
  libudev-dev
```

---

## 2️⃣ Build Hamlib from Source

Install Hamlib into a local prefix:

```bash
mkdir -p ~/src ~/build ~/local
cd ~/src
git clone https://github.com/Hamlib/Hamlib hamlib
cd hamlib
./bootstrap
```

Configure and build:

```bash
mkdir -p ~/build/hamlib/release
cd ~/build/hamlib/release

~/src/hamlib/configure \
  --prefix=$HOME/local/hamlib \
  --enable-shared \
  --disable-static \
  --without-cxx-binding

make -j"$(nproc)"
make install
```

Verify:

```bash
~/local/hamlib/bin/rigctl --version
```

---

## 3️⃣ Clone WSJT-64

```bash
cd ~/src
git clone https://github.com/mkveksas/wsjt-64
```

---

## 4️⃣ Configure WSJT-64

```bash
mkdir -p ~/build/wsjt-64/release
cd ~/build/wsjt-64/release

HL=$HOME/local/hamlib

cmake -S ~/src/wsjt-64 -B . -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_FLAGS="-Wno-error=deprecated-declarations" \
  -DCMAKE_CXX_FLAGS="-Wno-error=deprecated-declarations" \
  -DCMAKE_PREFIX_PATH="$HL" \
  -DWSJT_GENERATE_DOCS=OFF \
  -DWSJT_SKIP_MANPAGES=ON \
  -DWSJT_ENABLE_OMNIRIG=OFF \
  -DCMAKE_INSTALL_PREFIX="$HOME/local/wsjt-64"
```

---

## 5️⃣ Build

```bash
cmake --build . -- -j"$(nproc)"
```

---

## 6️⃣ Run WSJT-64

Because Hamlib is installed locally:

```bash
export LD_LIBRARY_PATH=$HOME/local/hamlib/lib:$LD_LIBRARY_PATH
./wsjtx
```
