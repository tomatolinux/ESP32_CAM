# ESP32 Camera Demo

tomatolinux@gmail.com
Code provided in this repository gets the image from camera and prints it out as ASCII art to the serial port.

## M5CAMERA
1Z实验室(1zlab): [中文文档](https://github.com/1zlab/esp-cam-tutorial)

## Build Status

[![Build Status](https://travis-ci.org/igrr/esp32-cam-demo.svg?branch=master)](https://travis-ci.org/igrr/esp32-cam-demo)

## Table of Contents
- [ESP32 Camera Demo](#esp32-camera-demo)
  - [M5CAMERA](#m5camera)
  - [Build Status](#build-status)
  - [Table of Contents](#table-of-contents)
  - [Components](#components)
    - [ESP32](#esp32)
    - [Camera](#camera)
    - [ESP-IDF](#esp-idf)
  - [Quick Start](#quick-start)
  - [Connect](#connect)
    - [Flash](#flash)
    - [Shoot](#shoot)
  - [How it Works](#how-it-works)
    - [Software](#software)
    - [Operation](#operation)
  - [Troubleshooting](#troubleshooting)
  - [Showcase](#showcase)
  - [Next Steps](#next-steps)
  - [Contribute](#contribute)
  - [Acknowledgments](#acknowledgments)

## Components

To make this code work, you need the following components:

* This repository. It contains submodules, so make sure you clone it with `--recursive` option. If you have already cloned it without `--recursive`, run `git submodule update --init`.
* [ESP32](https://espressif.com/en/products/hardware/esp32/overview) module
* Camera module
* PC with [esp-idf](https://github.com/espressif/esp-idf)

See the following sections for more details.

### ESP32

Any ESP32 module should work, if it has sufficient number of GPIO pins available to interface with camera. See section [Connect](#connect) for more details.

If you are an owner of [ESP-WROVER V1 (aka DevKitJ)](http://dl.espressif.com/dl/schematics/ESP32-DevKitJ-v1_sch.pdf), then camera connector is already broken out.

### Camera

This example has been tested with OV7725 camera module. Use it, if this is your first exposure to interfacing a microcontroller with a camera.

Other OV7xxx series should work as well, with some changes to camera configuration code. OV5xxx can work too, but it is advisable to choose the ones which support RGB or YUV 8-bit wide output bus. The ones which only output 10-bit raw data may be a bit harder to work with. Also choose the camera which can output a scaled down (QVGA or VGA) image. Use of larger frame buffers will require external SPI RAM.

### ESP-IDF

Configure your PC according to [ESP32 Documentation](http://esp-idf.readthedocs.io/en/latest/?badge=latest). [Windows](http://esp-idf.readthedocs.io/en/latest/windows-setup.html), [Linux](http://esp-idf.readthedocs.io/en/latest/linux-setup.html) and [Mac OS](http://esp-idf.readthedocs.io/en/latest/macos-setup.html) are supported. If this is you first exposure to ESP32 and [esp-idf](https://github.com/espressif/esp-idf), then get familiar with [01_hello_world](https://github.com/espressif/esp-idf/tree/master/examples/01_hello_world) and [02_blink](https://github.com/espressif/esp-idf/tree/master/examples/02_blink) examples. Make them work and understand before proceeding further.

## Quick Start

If you have your components ready, follow this section to [connect](#connect) the camera to ESP32 module, [flash](#flash) application to the ESP32 and finally [shoot](#shoot) and display the image.

## Connect

Specific pins used in this example to connect ESP32 and camera are shown in table below. Pinout can be adjusted to some extent in software. Table below provides two options of pin mapping (last two columns).

Notes:

1. **Important:** Make the connections short or you are likely to get noisy or even not legible images. More on that is discussed in section [Showcase](#showcase)
2. **Camera pin** column refers to pinout on OV7725 camera module
3. **Camera Power Down** pin does not need to be connected to ESP32 GPIO. Instead it may be pulled down to ground with 10 kOhm resistor.
4. OV7725 supports 10 bit image pixels. In this example the upper 8 bits are processed and saved. The pins corresponding with LSB are marked D0 and D1 and are left not connected.

If you have [ESP-WROVER V1 (aka DevKitJ)](http://dl.espressif.com/dl/schematics/ESP32-DevKitJ-v1_sch.pdf), then camera connector is already broken out and labeled Camera / JP4. Solder 2.54 mm / 0.1" double row, 18 pin socket in provided space and plug the camera module right into it. Line up 3V3 and GND pins on camera module and on ESP-WROVER. D0 and D1 should be left not connected outside the socket.

To connect the camera to Core Board V2 (aka DevKitC), consider alternate pin mapping (see the last column of table) that provides clean wiring layout shown below.

![alt text](pictures/ov7725-alternate-wiring.png "Wiring for Core Board V2 (aka DevKitC)")

2.2uF capacitor conencted between GND and EN pins of ESP32module is added to resolve [ESP32 Reset To Bootloader Issues on Windows #136](https://github.com/espressif/esptool/issues/136).

### Flash

Clone the code provided in this repository to your PC, compile with the latest [esp-idf](https://github.com/espressif/esp-idf) installed from GitHub and download to the module.

If all h/w components are connected properly you are likely to see the following message during download:

```
Krzysztof@tdk-kmb-op780 MSYS /esp/esp32-cam-demo
$ make flash
Flashing binaries to serial port com18 (app at offset 0x10000)...
esptool.py v2.0-dev
Connecting...

A fatal error occurred: Failed to connect to ESP32: Timed out waiting for packet header
make: *** [C:/msys32/esp-idf/components/esptool_py/Makefile.projbuild:48: flash] Error 2
```
This is due to a pullup on the camera reset line. It is stronger than the internal pull-down on `GPIO2` of the ESP32, so the chip cannot go into programming mode.

There are couple of options how to resolve this issue:

* If you are using ESP-WROVER V1 then connect GPIO2 to GND while flashing.
* Power down the camera module by removing it from the socket (ESP-WROVER V1) or by uplugging 3.3V wire.
* Map Camera Reset line to another GPIO pin on ESP32, for instance `GPIO15`.

### Shoot

Once module is loaded with code, open a serial terminal.

Camera demo application will first configure XCLK output that is timing operation of the camera chip.

```
D (1527) camera: Enabling XCLK output
I (1527) ledc: LEDC_PWM CHANNEL 0|GPIO 21|Duty 0004|Time 0
```
This clock is also timing output of pixel data on camera output interface - see I2S and DMA described below.

Then [SCCB](http://www.ovt.com/download_document.php?type=document&DID=63) interface is set up:


## How it Works

### Software

The core of camera software is contained in `camera` folder and consists of the following files.

* [camera.c](components/camera/camera.c) and [include/camera.h](components/camera/include/camera.h) - main file responsible for configuration of ESP32's GPIO, clock, I2S and DMA to interface with camera module. Once interface is established, it perfroms camera configuration to then retrieve image and save it in ESP32 memory. Access to camera is executed using lower level routines in the following files.

* [ov7725.c](components/camera/ov7725.c), [ov7725.h](components/camera/ov7725.h), [ov7725_regs.h](components/camera/ov7725_regs.h) and [sensor.h](components/camera/sensor.h) - definition of registers of OV7725 to configure camera funcinality. Functions to set register groups to reset camera to default configuration and configure specific functionality like resolution or pixel format. Setting he registers is performed by lower level function in files below.

* [sccb.c](components/camera/sccb.c) and [sccb.h](components/camera/sccb.h) - implementation of [Serial Camera Control Bus (SCCB)](http://www.ovt.com/download_document.php?type=document&DID=63) protocol to set camera registers.

* [twi.c](components/camera/twi.c) and [twi.h](components/camera/twi.h) - implementation of software I2C routines used by SCCB protocol.

* [wiring.c](components/camera/wiring.c) and [wiring.h](components/camera/wiring.h) - the lowest level routines to set GPIO pin mode, set GPIO pin level and delay program execution by required number of ms.

* [component.mk](components/camera/component.mk) - file used by C `make` command to access component during compilation.

* [Kconfig.projbuild](components/camera/Kconfig.projbuild) - file used by `make menuconfig` that provides menu option to switch camera test pattern on / off.

All above are called _esp-idf component_ and placed in `components` folder. Esp-idf framework provides `components` folder as a standard place to add modular functionality to a project.

Application starts and the top level control is executed from [app_main.c](main/app_main.c) file located in [main](main) folder.

### Operation

Interconnections between application and h/w internals of ESP32 to acquire an image from the camera is shown on diagram below.

![alt text](pictures/sw-operation-diagram.png "Operation diagram of camera application")

## Troubleshooting

If you have issues to get the live image right, enable test pattern and see what is retrieved.

To do so, run `make menuconfig`, open `Example Configuration` menu option and check `[ ] Enable test pattern on camera output`.

Optionally change the following define in file `camera.c`:

```
# define ENABLE_TEST_PATTERN CONFIG_ENABLE_TEST_PATTERN
```

Camera sensor will then output test pattern instead of live image.

```
```
The value in brackets with % sign provides number of frames that differ from the test pattern. See code inline comments for more information on this functionality.

## Showcase

This code has been tested with hardware presented below.


## Next Steps

We are planning to test and compare images captured using:

* ESP-WROVER V1 with camera module directly plugged in.
* Core Board V2 and camera module interconnected with a daughter board instead of loose cables.

In longer perspective we plan the following:

* Describe what's inside [camera component](https://github.com/igrr/esp32-cam-demo/tree/master/components/camera)
* LCD support
* QR Code reading
* Web interface to control the camera
* Camera component API development

## Contribute

You are welcome to contribute to this repository by providing documentation to code, submitting issue reports, enhancement requests and pull requests. Contributions should follow [Contributions Guide](http://esp-idf.readthedocs.io/en/latest/contributing.html) section of ESP32 Programming Guide.

## Acknowledgments

This application is using code developed by:
* OpenMV implementation for OV7725 by Ibrahim Abdelkader
* Software I2C library for ESP31B by Hristo Gochkov
