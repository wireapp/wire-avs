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


Build Instructions
------------------

AVS will work with Google WebRTC which first needs to be pulled from Google and pre-compiled. To do that go to the webrtc directory and run:

./scripts/build.sh ./scripts/package.sh

This should be done on OSX for headers and iOS+OSX libs and on Linux for Linux+Android libs. The scripts pull from Googles repos, apply a patch for Android (in patches dir), build and package the libs into zips, which get copied to contrib/webrtc. Expected zips are:

contrib/webrtc/webrtc_72.local_android.zip contrib/webrtc/webrtc_72.local_headers.zip contrib/webrtc/webrtc_72.local_ios.zip contrib/webrtc/webrtc_72.local_linux.zip contrib/webrtc/webrtc_72.local_osx.zip

All zips are needed to build AVS, but can be empty files if not needed: e.g. you can run touch contrib/webrtc/webrtc_72.local_android.zip if you dont intend to build for Android.


AVS has more dependencies that need to be updated. The first time you need to fetch the submodules by doing:

```
$ ./prepare.sh
```

Next step is to build AVS itself. When building AVS with local webrtc, invoke make with:

make WEBRTC_VER .local

For example:

make WEBRTC_VER=72.local

The deliverables are being built by saying make WEBRTC_VER <release>.local dist. You can limit this to only select target platforms through make WEBRTC_VER <release>.local dist_android, make WEBRTC_VER <release>.local dist_osx and make WEBRTC_VER <release>.local dist_ios. All of them take quite a while on a fresh checkout.

If you simply say ``make``, a pre-compiled version of webrtc is included, and a selection of tools is being built for your
host machine. You probably want ``zcall``, the AVS command line client.
You can only build that by saying ``make zcall``. Similarly, you can
build any other tool by giving its name to make.

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
-----------------------------

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


Maintainer Instructions
-----------------------

### Making Releases

When making a new release, follow these steps:

* Update the ChangeLog.txt file. See the older entries on how that should
  look.

* Branch off the new release by creating a branch release-x.y where x.y
  is the release number and push it to origin (no, you can't just copy
  and paste the following snippet):

```bash
$ git checkout -b release-x.y
$ git push --set-upstream origin release-x.y
```

* Go back to master and update `Makefile` to have the number of the _next_
  relase in `VER_MAJOR` and `VER_MINOR`. Push your changes.

```bash
$ git checkout master
$ vim Makefile
$ git push
```

* Go to Jenkins at http://[IP]:8080/job/avs-release/configure
  and change "Branches to build" to the name of your new release branch.
  Save and "build now".

* Once Jenkins is done, issue Pull Requests to zclient-android,
  zclient-ios, and zclient-osx.


### Updating Contrib Libraries from Upstream

The build system uses `git ls-files` to determine the build dependencies
for `contrib` libraries in a vaguely reliable way. It is therefore
important that only files are checked in that are not being updated on
every build. Else that contrib library is re-build on every `make`
defeating the purpose.

Unfortunately, some source packages contains such files (notably, OpenSSL
is a big giant mess). When updating such a package, make sure to delete
these files before your `git add`.

The easiest way to achieve this is by adding your new files, run a
`make contrib_all` (or `make contrib_your_library`) and, if building
succeeded, find the offending files by running

```bash
$ ls -latr `git ls-files contrib/your-library`
```

This will print all files in order of last access with newest files last.
Find those that have been modified during the last build and carefully
remove them.

Please also check `contrib/.gitignore` that it contains all these files.


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
