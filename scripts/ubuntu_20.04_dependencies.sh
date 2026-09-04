#!/usr/bin/env bash

# dependencies to build wire-avs
# on ubuntu 18.04 and 20.04
# for building webrtc see https://github.com/wireapp/prebuilt-webrtc-binaries
# 1. Install base utilities and update root certificates first
apt-get update && apt-get install -y \
    ca-certificates \
    wget \
    gnupg \
    lsb-release \
    software-properties-common \
    && rm -rf /var/lib/apt/lists/*

# 2. Now run the LLVM script safely (it won't fail the TLS handshake)
wget https://apt.llvm.org/llvm.sh \
    && chmod +x llvm.sh \
    && ./llvm.sh 19 \
    && rm llvm.sh \
    && rm -rf /var/lib/apt/lists/*

apt update

apt install -y \
    ca-certificates \
    curl \
    git \
    autoconf \
    automake \
    cargo \
    clang-19 \
    clang-tools-19 \
    lld-19 \
    libc++-19-dev \
    libc++abi-19-dev \
    clang-tools \
    jq \
    libasound2-dev \
    libevent-dev \
    libprotobuf-c-dev \
    libreadline-dev \
    libsodium-dev \
    libtool \
    libx11-dev \
    libxcomposite-dev \
    libxdamage-dev \
    libxrender-dev \
    make \
    openjdk-17-jdk-headless \
    openjdk-17-jre-headless \
    pkgconf \
    protobuf-c-compiler \
    python3 \
    python-is-python3 \
    python3-pip \
    python3-six \
    rsync \
    yasm \
    zlib1g-dev \
    zip \
    libssl-dev \
    libsctp-dev \
    libpulse-dev \
    valgrind \
    lsb-release \
    wget \
    software-properties-common \
    gnupg2

# uninstall distribution version of cargo/rust
apt purge -y cargo rustc

# cleanup apt cache to reduce image size
apt clean

# Setup Clang 19 alternatives and install Rust
update-alternatives --install /usr/bin/clang clang /usr/bin/clang-19 100
update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-19 100

# download the rust toolchain (to build the cryptobox-c dependency)
curl https://sh.rustup.rs -sSf | sh -s -- -y

# Downgrade to rust 1.71.1 to mitigate https://github.com/rust-lang/rust/issues/123285
/build/avs/.cargo/bin/rustup install 1.71.1
/build/avs/.cargo/bin/rustup default 1.71.1-x86_64-unknown-linux-gnu
