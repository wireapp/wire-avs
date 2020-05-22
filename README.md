# Wire - Audio, Video, and Signaling (AVS)

This repository is part of the source code of Wire. You can find more information at [wire.com](https://wire.com) or by contacting opensource@wire.com.

You can find the published source code at [github.com/wireapp](https://github.com/wireapp).

For licensing information, see the attached LICENSE file and the list of third-party licenses at [wire.com/legal/licenses/](https://wire.com/legal/licenses/).

## Build Requirements

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
Debian-esque system:

* If you use ubuntu 18.04: `sudo ./scripts/ubuntu_18.04_dependencies.sh`
* If you use debian 10 (buster): `sudo ./scripts/debian_10_dependencies.sh`

For **Windows**, you will have to start by adding your system to the build
system. Good luck!


## Build Instructions


AVS will work with Google WebRTC which first needs to be pulled from Google and pre-compiled. To do that go to the webrtc directory and run:

```
./scripts/build.sh
./scripts/package.sh
```

This should be done on OSX for headers and iOS+OSX libs and on Linux for Linux+Android libs. The scripts pull from Googles repos, apply a patch for Android (in patches dir), build and package the libs into zips, which get copied to contrib/webrtc. Expected zips are:

```
contrib/webrtc/webrtc_m79.local_android.zip
contrib/webrtc/webrtc_m79.local_headers.zip
contrib/webrtc/webrtc_m79.local_ios.zip
contrib/webrtc/webrtc_m79.local_linux.zip
contrib/webrtc/webrtc_m79.local_osx.zip
```

All zips are needed to build AVS, but can be empty files if not needed: e.g. you can run touch contrib/webrtc/webrtc_m79.local_android.zip if you dont intend to build for Android.

AVS has more dependencies that need to be updated with:

```
./prepare.sh
```

Next step is to build AVS itself. When building AVS with local webrtc, invoke make with:

```
make WEBRTC_VER <release>.local
```

For example:

```
make WEBRTC_VER=m79.local
```

The deliverables are being built by saying ``make WEBRTC_VER <release>.local dist``. You can limit
this to only select target platforms through ``make WEBRTC_VER <release>.local dist_android``,
``make WEBRTC_VER <release>.local dist_osx`` and ``make WEBRTC_VER <release>.local dist_ios``. All of them take quite a while
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
$ make WEBRTC_VER=m79.local dist_ios DIST_ARCH=armv7
```

will build an iOS distribution that will only contain armv7 instead of
the usual five architectures.



## Using the Library

During the build, a set of static libraries is being built. You can use
this library in your own projects. 

You'll find the APIs in ``include/*.h``. ``avs.h`` is your catchall
include file. Always use that to protect yourself agains reorganizations.

Linking is a bit tricky, we'll add instructions soon. The easiest is
probably to add ``build/$(your-platform)/lib`` to your library path and
then add all ``.a`` files in there as ``-l`` arguments.


## Tools

A tool for testing basic functionality called zcall can be built with e.g.

``make WEBRTC_VER=m79.local zcall``

Use ``zcall -help`` to see available options. To log into the client:

``./zcall -e <Wire-login-email> -p <password>``

Next step is to register a client:

``:reg_client``

Please note that only 7 clients can be registered and it may become necessary to delete a client before a new one can be registered.


## Architecture overview:


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




## Some specifications implemented:

* https://tools.ietf.org/html/draft-ietf-mmusic-trickle-ice-01
* https://tools.ietf.org/html/draft-ietf-rtcweb-stun-consent-freshness-11
* https://tools.ietf.org/html/rfc7845
* https://tools.ietf.org/html/draft-ietf-rtcweb-data-channel-13


## Reporting bugs

When reporting bugs against AVS please include the following:

- Wireshark PCAP trace ([download Wireshark](https://www.wireshark.org/download.html))
- Full logs from client
- Session-ID
- Which Backend was used
- Exact version of client
- Exact time when call was started/stopped
- Name/OS of device
- Adb logcat for Android

