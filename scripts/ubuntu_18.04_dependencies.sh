#!/usr/bin/env bash

set -e

if [ "$EUID" -ne 0 ]
  then echo "Please run as root"
  exit 1
fi

# dependencies to build
#   * webrtc
#   * wire-audio-video-signalling
# on ubuntu 18.04
apt-get install -y \
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
    xcompmgr \
    uuid-dev \
    subversion \
    ruby \
    python-yaml \
    python-psutil \
    python-openssl \
    python-opencv \
    python-numpy \
    python-dev \
    python-crypto \
    python-cherrypy3 \
    php7.2-cgi \
    openbox \
    libxtst-dev \
    libxss-dev \
    libxslt1-dev \
    libxkbcommon-dev \
    libwww-perl \
    libwayland-egl1-mesa \
    libudev-dev \
    libssl-dev \
    libsqlite3-dev \
    libspeechd-dev \
    libspeechd2 \
    libsctp-dev \
    libpulse-dev \
    libpci-dev \
    libpango1.0-0 \
    libnss3-dev \
    libnspr4-dev \
    libkrb5-dev \
    libjpeg-dev \
    libgtk-3-dev \
    libgtk-3-0 \
    libgnome-keyring-dev \
    libgnome-keyring0 \
    libglu1-mesa-dev \
    libglib2.0-dev \
    libgbm-dev \
    libffi-dev \
    libelf-dev \
    libdrm-dev \
    libcurl4-gnutls-dev \
    libcups2-dev \
    libcap-dev \
    libcairo2-dev \
    libbz2-dev \
    libbrlapi-dev \
    libbrlapi0.6 \
    libbluetooth-dev \
    libatspi2.0-dev \
    libappindicator3-dev \
    libappindicator3-1 \
    libapache2-mod-php7.2 \
    gperf \
    g++-7-multilib \
    fakeroot \
    elfutils \
    devscripts \
    dbus-x11 \
    binutils-mipsel-linux-gnu \
    binutils-mips64el-linux-gnuabi64 \
    binutils-arm-linux-gnueabihf \
    binutils-aarch64-linux-gnu \
    x11-utils \
    rpm \
    p7zip \
    libxt-dev \
    rsync \
    apache2-bin

dpkg --add-architecture i386
apt-get update
apt-get install -y \
    linux-libc-dev:i386 \
    libx11-xcb1:i386 \
    libpci3:i386

# download the rust toolchain (to build the cryptobox-c dependency)
curl https://sh.rustup.rs -sSf | sh -s -- -y
