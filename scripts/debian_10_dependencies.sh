#!/usr/bin/env bash

set -e
if [ "$EUID" -ne 0 ]
  then echo "Please run as root"
  exit 1
fi

# dependencies to build
#   * webrtc
#   * wire-audio-video-signalling (linux targets)
# on debian 10
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
    p7zip \
    libxt-dev \
    realpath \
    rsync \
    apache2-bin

dpkg --add-architecture i386
apt-get update
apt-get install -y \
    linux-libc-dev:i386 \
    libx11-xcb1:i386 \
    libpci3:i386

# we need some older packages, so get them from an older debian version, uh..
grep stretch /etc/apt/sources.list || echo 'deb https://deb.debian.org/debian stretch main contrib non-free' >> /etc/apt/sources.list
apt-get update
apt-get install -y libgnome-keyring-dev libgnome-keyring0

# we need some even older packages, so get them from an even older debian version, uh..
grep jessie /etc/apt/sources.list || echo 'deb https://deb.debian.org/debian jessie main contrib non-free' >> /etc/apt/sources.list
apt-get update
apt-get install -y libapache2-mod-php5 php5-cgi

# https://stackoverflow.com/questions/57031649/how-to-install-openjdk-8-jdk-on-debian-10-buster
apt-get install -y software-properties-common
add-apt-repository --yes https://adoptopenjdk.jfrog.io/adoptopenjdk/deb/
apt-get update
apt-get install -y adoptopenjdk-8-hotspot

# if you have multiple versions of java, configure jdk 8 using this:
# update-alternatives --config java

# download the rust toolchain (to build the cryptobox-c dependency)
curl https://sh.rustup.rs -sSf | sh -s -- -y
