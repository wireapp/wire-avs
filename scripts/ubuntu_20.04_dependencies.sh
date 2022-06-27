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
    openjdk-8-jdk-headless \
    pkgconf \
    protobuf-c-compiler \
    python3 \
    python-is-python3 \
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
rm -rf /var/lib/apt/lists/

# download the rust toolchain (to build the cryptobox-c dependency)
curl https://sh.rustup.rs -sSf | sh -s -- -y

