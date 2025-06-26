# ImpulseLoader

This is a simple, mono, IR-File loader/convolution LV2, clap, vst2 plug/ Stans-alone application. 

![ImpulseLoader](https://raw.githubusercontent.com/brummer10/ImpulseLoader/master/ImpulseLoader.png)

A Stereo version of ImpulseLoader is here [ImpulseLoaderStereo](https://github.com/brummer10/ImpulseLoaderStereo.lv2)

## Supported File Formats:

- WAF
- AIFF
- WAVEFORMATEX
- CAF

IR-Files could be loaded via the integrated File Browser, or, when supported by the host, via drag and drop.

If the IR-File have more then 1 channel, only the first channel will be used.

IR-Files will be resampled on the fly to match the session Sample Rate.

## Dependencies

- libsndfile1-dev
- libfftw3-dev
- libcairo2-dev
- libx11-dev
- lv2-dev

## Build

[![build](https://github.com/brummer10/ImpulseLoader/actions/workflows/build.yml/badge.svg)](https://github.com/brummer10/ImpulseLoader/actions/workflows/build.yml)

To build ImpulseLoader only as standalone application run
```shell
make standalone
```

To build ImpulseLoader only as Clap plugin run
```shell
make clap
```

To build ImpulseLoader only as vst2 plugin run
```shell
make vst2
```

To build ImpulseLoader with all favours (currently as LV2, Clap and vst2 plugin and as standalone application) run
```shell
make
```

## Building LV2 plug from source code

```shell
git clone https://github.com/brummer10/ImpulseLoader
git submodule init
git submodule update
make lv2 # build the LV2 plug
make install # will install into ~/.lv2 ... AND/OR....
sudo make install # will install into /usr/lib/lv2
```

