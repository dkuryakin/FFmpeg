set -xe

sudo apt-get install -y \
    autoconf \
    automake \
    build-essential \
    cmake \
    git \
    libtool \
    pkg-config \
    yasm \
    nasm \
    libx264-dev \
    libx265-dev libnuma-dev \
    zlib1g-dev

TARGET_DIR=$(pwd)/ffmpeg_build

PATH=$TARGET_DIR/bin:$PATH ./configure \
    --prefix="$TARGET_DIR" \
    --pkg-config-flags="--static" \
    --extra-cflags="-I$TARGET_DIR/include" \
    --extra-ldflags="-L$TARGET_DIR/lib" \
    --extra-libs="-lpthread -lm" \
    --bindir="$TARGET_DIR/bin" \
    --ld="g++" \
    --enable-gpl \
    --enable-libx264 \
    --enable-libx265 \
    --disable-ffplay \
    --disable-debug \
    --disable-doc \
    --disable-programs \
    --disable-ffprobe \
    --enable-ffmpeg

PATH=$TARGET_DIR/bin:$PATH make -j$(nproc)
make install
