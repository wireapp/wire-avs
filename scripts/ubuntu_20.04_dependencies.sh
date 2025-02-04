#!/usr/bin/env bash

# dependencies to build wire-avs
# on ubuntu 18.04 and 20.04
# for building webrtc see https://github.com/wireapp/prebuilt-webrtc-binaries
apt update

apt install -y \
    curl \
    git \
    autoconf \
    automake \
    cargo \
    clang \
    clang-tools \
    jq \
    libasound2-dev \
    libc++-dev \
    libc++abi-dev \
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
    valgrind

# uninstall distribution version of cargo/rust
apt purge -y cargo rustc

# cleanup apt cache to reduce image size
apt clean


# download the rust toolchain (to build the cryptobox-c dependency)
curl https://sh.rustup.rs -sSf | sh -s -- -y

# Downgrade to rust 1.71.1 to mitigate https://github.com/rust-lang/rust/issues/123285
rustup install 1.71.1
rustup default 1.71.1-x86_64-unknown-linux-gnu
