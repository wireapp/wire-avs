librem README
=============


librem is a Audio and video processing media library
Copyright (C) 2010 - 2016 Creytiv.com

[![Build Status](https://travis-ci.org/creytiv/rem.svg?branch=master)](https://travis-ci.org/creytiv/rem)


## Features

* Audio buffer
* Audio sample format conversion
* Audio file reader/writer
* Audio mixer
* Audio resampler
* Audio tone generator
* Audio codec (G.711)
* Video mixer
* Video pixel converter
* FIR-filter


## Building

librem is using GNU makefiles, and [libre](https://github.com/creytiv/re)
must be installed before building.


### Build with default options

```
$ make
$ sudo make install
$ sudo ldconfig
```


## Documentation

The online documentation generated with doxygen is available in
the main [website](http://creytiv.com/doxygen/rem-dox/html/)


## License

The librem project is using the BSD license.


## Contributing

Patches can sent via Github
[Pull-Requests](https://github.com/creytiv/rem/pulls) or to the RE devel
[mailing-list](http://lists.creytiv.com/mailman/listinfo/re-devel).
Currently we only accept small patches.
Please send private feedback to libre [at] creytiv.com


## Modules
```
Audio Modules:

  name:     status:       description:

* au        testing       Base audio types
* aubuf     testing       Audio buffer
* auconv    unstable      Audio sample format conversion
* aufile    testing       Audio file reader/writer
* aumix     unstable      Audio mixer
* auresamp  unstable      Audio resampler
* autone    testing       Tone/DTMF generator
* g711      stable        G.711 audio codec




Video Modules:

  name:     status:       description:

* vid       testing       Base video types
* vidconv   testing       Colorspace conversion and scaling
* vidmix    unstable      Video mixer




Generic modules:

* dsp       testing       DSP routines
* fir       unstable      FIR (Finite Impulse Response) filter
```




## Specifications:

* ITU-T G.711 Appendix I and Appendix II


## Supported platforms

Same as [libre](https://github.com/creytiv/re)


## Related projects

* [libre](https://github.com/creytiv/re)
* [retest](https://github.com/creytiv/retest)
* [baresip](https://github.com/alfredh/baresip)



## References

http://creytiv.com/rem.html

https://github.com/creytiv/rem

http://lists.creytiv.com/mailman/listinfo/re-devel
