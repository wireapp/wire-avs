Wire - Audio, Video, and Signaling (AVS)
=============================

This repository is part of the source code of Wire. You can find more information at wire.com or by contacting opensource@wire.com.

You can find the published source code at github.com/wireapp.

For licensing information, see the attached LICENSE file and the list of third-party licenses at wire.com/legal/licenses/.


Build Requirements
------------------

Apart from the basic toolchain for each system, you need these:

* clang, libc++
* readline (for building zcall, only)
* yasm (for video only)
* alsa (for Linux only).

For **OSX** and **iOS**, you should have Xcode and the Command Line Tools
for your specific version of both OSX and Xcode. Things *will* break if
you have the wrong version. You can install the latter via menu Xcode,
then Open Developer Tool, then More Developer Tools.

For getting autoconf, automake, libtool, readline and yasm, we suggest [Homebrew](http://brew.sh/).
Follow the instructions there, then:

```bash
$ brew install \
  autoconf \
  automake \
  jq \
  libsodium \
  libtool \
  rust \
  pkg-config \
  protobuf-c \
  readline
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
  zip \
  libglib2.0-dev \
  cargo
```
Which should be working for
- Ubuntu 24.04.2 wire LTS amd64 (Cubic 2025-02-23 17:20)
- Ubuntu 24.04.3 LTS


For **Windows**, you will have to start by adding your system to the build
system. Good luck!


Build Instructions
------------------

AVS uses pre-built Google WebRTC by default that are pulled from the [prebuilt webrtc repository](https://github.com/wireapp/prebuilt-webrtc-binaries) as a part of the make process. For information about building your own WebRTC see the "Using a Locally Built WebRTC" section below.

AVS has more dependencies that need to be updated. The first time you need to fetch the submodules by doing:

```bash
$ ./prepare.sh
```

Next step is to build AVS itself. When building AVS with the prebuilt WebRTC, invoke make with:

```bash
make
```

This will build a selection of tools or your host machine. You probably want ``zcall``, the AVS command line client. You can only build that by saying ``make zcall``. Similarly, you can build any other tool by giving its name to make.

The deliverables are being built with the command ``make dist``.
You can limit this to only select target platforms through ``make dist_android``,
``make dist_osx`` and ``make dist_ios``. All of them take quite a while on a
fresh checkout.

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
$ make dist_ios DIST_ARCH=arm64
```

will build an iOS distribution that will only contain arm64 instead of
the usual five architectures.

Using a Locally Built WebRTC
----------------------------

It is possible to use your own locally built WebRTC libraries instead, by following the instructions in the readme file of the [prebuilt webrtc repository](https://github.com/wireapp/prebuilt-webrtc-binaries).

Once built and packaged you should have the following files:

```
contrib/webrtc/webrtc_<version>_android.zip
contrib/webrtc/webrtc_<version>_headers.zip
contrib/webrtc/webrtc_<version>_ios.zip
contrib/webrtc/webrtc_<version>_linux.zip
contrib/webrtc/webrtc_<version>_osx.zip
```

These files should be copied to the contrib/webrtc directory and the ``WEBRTC_VER`` variable set when making AVS, for example:

```bash
make WEBRTC_VER=20200603.local
```

You can also modify the version set in the ``mk/target.mk`` file, as follows:

```
ifeq ($(WEBRTC_VER),)
WEBRTC_VER := 20200603.local
endif
```

Running `make` should then unpack and use the locally built version of WebRTC.

Using the Library
-----------------

During the build, a set of static libraries is being built. You can use
this library in your own projects.

You'll find the APIs in ``include/*.h``. ``avs.h`` is your catchall
include file. Always use that to protect yourself agains reorganizations.

Linking is a bit tricky, we'll add instructions soon. The easiest is
probably to add ``build/$(your-platform)/lib`` to your library path and
then add all ``.a`` files in there as ``-l`` arguments.


Using the Command Line Client (zcall)
-------------------------------------

Start the command line client provding the email address of an existing
account using the ``-e`` option. You can switch to staging (aka dev) by
adding the ``-D`` option and to edge by adding the ``-E`` option. Since
caching is currently a little broken, you probably want to add the ``-f``
option, too. For further information on available options, try the ever
helpful ``-h`` option.

Once started, hit ``h`` to see a list of key strokes available and
type ``:help`` and enter to see a list of commands. All commands are
entered by typing ``:`` first.

**Creating a Client**

The first thing you will need is a clientid. This can be done as follows:

`:get_clients`    lists clients for this user, the current one for zcall is marked with a `*`
`:reg_client`     register a new client

There is a limit of 8 clients per user, if all are used you will need to remove one with:

`:delete_cient <clientid>`

Beware that there is no "are you sure" question, use this only if you know what you are doing! If you delete an in-use client by accident bad things may happen.

**Managing Conversations**

Keys for listing, selecting and showing conversations are:

`l` list conversations, the selected one is marked with `->`
`j` select previous conversation
`k` select next conversation
`i` show selected conversation ID and members

You can also select a conversation with the `:switch` command and send basic chat messages to the selected conversation with `:say`

**Calling**

Keys for calling are:

`c` start a call in the selected conversation
`a` answer the most recent incoming call
`e` end/leave the call
`m` toggle mute
`V` toggle video sending

Incoming calls are indicated by the following line:
```
calling: incoming audio call in conv: Conversation (conference) from "test_user:0123456789abcdef" ring: yes ts: 1614244695
```


Architecture overview:
----------------------


```
           .-----------.                            .---------.  .----------.
           |   wcall   |                            | engine  |  | mediamgr |
           '-----------'                            '---------'  '----------'
            /    |    \                                  |
  .-----------.  |  .-----------.   .----------.    .---------.
  |  egcall   |  |  |   ccall   |---| keystore |    |  REST   |
  '-----------'  |  '-----------'   '----------'    |  nevent |
            \    |    /                             | protobuf|
           .-----------.   .-----------.            '---------'
           |   ecall   |---|  econn    |
           '-----------'   '-----------'
             /        \
     mobile /          \ web
  .-----------.     .-----------.
  | peerflow  |     |  jsflow   |
  '-----------'     '-----------'
        |                 |
  .-----------.     .-----------.
  |webrtc(C++)|     | avs_pc(JS)|
  | peerconn  |     '-----------'
  '-----------'           |
                    .-----------.
                    | webrtc(JS)|
                    | peerconn  |
                    '-----------'

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


Reporting bugs
--------------

When reporting bugs against AVS please include the following:

- Wireshark PCAP trace ([download Wireshark](https://www.wireshark.org/download.html))
- Full logs from client
- Session-ID
- Which Backend was used
- Exact version of client
- Exact time when call was started/stopped
- Name/OS of device
- Adb logcat for Android

Run-time libraries
------------------
FROM ubuntu:16.04
RUN apt-get install -qqy --no-install-recommends \
    	libprotobuf-c-dev \
    	libc6-dev-i386 \
        libreadline-dev \
        libx11-dev \
        libxcomposite-dev \
        libxdamage-dev \
        libxrender-dev \
        libc++-dev \
        libc++abi-dev


Upload to sonatype
------------------

To manually upload to sonatype create a local.properties with the following values:

```
sonatype.username=
sonatype.password=
signingKeyFile=<path to asc file>
signingPassword=<gpg key passphrase>
```
