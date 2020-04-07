# ESP32 Digitally Controlled Oscillator

[![Platform: ESP-IDF](https://img.shields.io/badge/ESP--IDF-v3.0%2B-blue.svg)](https://docs.espressif.com/projects/esp-idf/en/stable/get-started/)
[![Build Status](https://travis-ci.org/DavidAntliff/esp32-i2c-lcd1602-example.svg?branch=master)](https://travis-ci.org/DavidAntliff/esp32-dco)
[![license](https://img.shields.io/github/license/mashape/apistatus.svg)]()

## Introduction

This software produces a square wave on a specified GPIO at the requested frequency.

This component can use the RMT peripheral or the LED\_PWM peripheral to accurately generate the signal.

The RMT approach works quite well, and can generate frequencies up to tens of megahertz, however it is complex to configure.

The LCD\_PWM approach is much simpler to configure, however it seems limited to frequencies from about 10 - 10,000 Hz. I have also had trouble getting the low-speed timers to work with the LED\_PWM.

Duty cycle can be configured with the LED\_PWM approach.

Choose your method in `app_main.c`.

It is written and tested for v3.3 of the [ESP-IDF](https://github.com/espressif/esp-idf) environment, using the xtensa-esp32-elf toolchain (gcc version 5.2.0).

Ensure that submodules are cloned:

    $ git clone --recursive https://github.com/DavidAntliff/esp32-dco.git

Build the application with:

    $ cd esp32-dco
    $ idf.py menuconfig
    $ idf.py build
    $ idf.py -p (PORT) flash monitor

## Source Code

The source is available from [GitHub](https://www.github.com/DavidAntliff/esp32-dco).

## License

The code in this project is licensed under the MIT license - see LICENSE for details.

## Roadmap

* Runtime determination of optimal clock divider with RMT approach.
* Select duty cycle with RMT approach. 
* Support for runtime variation of frequency, possibly via MQTT command.
* Support for phase inversion.
* Support for multiple channels and phase shifting between channels.
* Investigate low-speed timer problems with LED\_PWM approach.
* Support for Build System selection of DCO method.
