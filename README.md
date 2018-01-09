# Wire - Audio, Video, and Signaling (AVS)

This repository is part of the source code of Wire. You can find more information at [wire.com](https://wire.com) or by contacting opensource@wire.com.

You can find the published source code at [github.com/wireapp](https://github.com/wireapp). 

For licensing information, see the attached LICENSE file and the list of third-party licenses at [wire.com/legal/licenses/](https://wire.com/legal/licenses/).

# Build Requirements

Apart from the basic toolchain for each system, you need these:

* clang, libc++
* yasm (for video only)
* alsa (for Linux only).

For **OSX** and **iOS**, you should have Xcode and the Command Line Tools
for your specific version of both OSX and Xcode. Things *will* break if
you have the wrong version. You can install the latter via menu Xcode,
then Open Developer Tool, then More Developer Tools.

For getting autoconf, automake, libtool, and yasm, we suggest [Homebrew](http://brew.sh/).
Follow the instructions there, then:

```bash
$ brew install \
  autoconf \
  automake \
  libsodium \
  libtool \
  multirust \
  pkg-config \
  protobuf-c \
  readline \
  sdl \
  yasm
$ multirust default nightly
```



For **Android**, you need both the
[Android SDK](https://developer.android.com/sdk/index.html) as well as the
[Android NDK](https://developer.android.com/tools/sdk/ndk/index.html).
Just get the latest versions and install them somewhere cozy. You need to
export two environment variables ``ANDROID_SDK_ROOT`` and
``ANDROID_NDK_ROOT`` pointing to the respective location. Unless you do a
one-off, you probably want to add them to your ``.bash_profile``.

For **Linux**, you need to install the packages for the stuff mentioned
above or, of course, build it all from scratch. If you are on a
Debian-esque system, do this: 

```bash
$ sudo apt-get install \
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
  zip

$ curl -sSf https://static.rust-lang.org/rustup.sh | sh -s -- --channel=nightly
```



For **Windows**, you will have to start by adding your system to the build
system. Good luck!


# Build Instructions

The first time you need to fetch the submodules by doing:

```
$ ./prepare.sh
```

Patches for webrtc are under mediaengine/webrtc_patches. Run apply_patches.sh to apply them

If you simply say ``make``, a selection of tools is being built for your
host machine.

The deliverables are being built by saying ``make dist``. You can limit
this to only select target platforms through ``make dist_android``,
``make dist_osx`` and ``make dist_ios``. All of them take quite a while
on a fresh checkout.

You'll find the deliverables in ``build/dist/{android,ios,osx}``.

You can also build just the wrappers for a given architecture by saying
``make wrappers AVS_OS=<os> AVS_ARCH=<arch>`` where ``<os>`` is one of
``android``, ``ios``, or ``osx``. There is no wrappers for Linux, so you
are out of luck there. For ``<arch>`` there are several possible values
depending on the OS. You can just leave the whole thing out and will
receive reasonable defaults (ARMv7 or X86-64). Have a look at
``mk/target.mk`` for more on this.


If you want to have a local version of a ``dist_*`` target that hasn't
all the necessary architectures but builds quicker, you can pass
``DIST_ARCH=<your_arch>`` to make and will only built for that
architecture:

```bash
$ make dist_ios DIST_ARCH=armv7
```

will build an iOS distribution that will only contain armv7 instead of
the usual five architectures.



# Using the Library

During the build, a set of static libraries is being built. You can use
this library in your own projects. 

You'll find the APIs in ``include/*.h``. ``avs.h`` is your catchall
include file. Always use that to protect yourself agains reorganizations.

Linking is a bit tricky, we'll add instructions soon. The easiest is
probably to add ``build/$(your-platform)/lib`` to your library path and
then add all ``.a`` files in there as ``-l`` arguments.


# Architecture overview:


```
                      .----------.  .------.
                      |  Engine  |  | Mill |
                      '----------'  '------'
                     /      |
    .--------------.        |
    |   EGCALL     |  .----------.  .-----------.  .----------.
    |   ECALL      |  | REST     |  | Media-mgr |  | Netprobe |
    |   ECONN      |  | Nevent   |  '-----------'  '----------'
    '--------------'  '----------'
       |      |
  .---------. |  .-----.
  |Mediaflow| |  | DCE |
  '---------'\|  '-----'
       |      |\                                   .----------.
   .-------.  | \.--------.                        | Protobuf |
   |aucodec|  |  |vidcodec|                        '----------'
   '-------'  |  '--------'
       |     / \     |                             .----------.
   .-------./   \.--------.                        | Conf-Pos |
   |  VOE  |     |  VIE   |                        '----------'
   '-------'     '--------'



    .------------------------------.
    | Low-level utility modules:   |
    | - audummy (Dummy audio-mod)  |
    | - base (Base module)         |
    | - cert (Certificates)        |
    | - dict (Dictionary)          |
    | - jzon (Json wrappers)       |
    | - log (Logging framework)    |
    | - queue (Packet queue)       |
    | - sem (Semaphores)           |
    | - store (Persistent Storage) |
    | - trace (Tracing tool)       |
    | - uuid (UUID helpers)        |
    | - zapi (ZETA-protocol API)   |
    | - ztime (Timestamp helpers)  |
    '------------------------------'
```




Some specifications implemented:
-------------------------------

* https://tools.ietf.org/html/draft-ietf-mmusic-trickle-ice-01
* https://tools.ietf.org/html/draft-ietf-rtcweb-stun-consent-freshness-11
* https://tools.ietf.org/html/rfc7845
* https://tools.ietf.org/html/draft-ietf-rtcweb-data-channel-13


# Reporting bugs

When reporting bugs against AVS please include the following:

- Wireshark PCAP trace ([download Wireshark](https://www.wireshark.org/download.html))
- Full logs from client
- Session-ID
- Which Backend was used
- Exact version of client
- Exact time when call was started/stopped
- Name/OS of device
- Adb logcat for Android

