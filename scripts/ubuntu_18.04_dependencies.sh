#!/usr/bin/env bash

set -e

if [ "$EUID" -eq 0 ]
  then echo "Please run as your user, this script uses sudo internally"
  exit 1
fi

# dependencies to build wire-avs
# on ubuntu 18.04 and 20.04
# for building webrtc see https://github.com/wireapp/prebuilt-webrtc-binaries
sudo apt update

sudo apt install -y \
    curl \
    git \
    autoconf \
    automake \
    clang \
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
    pkgconf \
    protobuf-c-compiler \
    yasm \
    zlib1g-dev \
    zip \
    libssl-dev \
    libsctp-dev \
    libpulse-dev \
    valgrind

# download the rust toolchain (to build the cryptobox-c dependency)
curl https://sh.rustup.rs -sSf | sh -s -- -y

