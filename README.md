# Irrigation Controller

This is the software repository for the Irrigation Controller project.

The hardware repository of the project can be found here (TBD).

I also blog about the project on [my website](https://www.matronix.de/tag/iop/) under the hashtag #IoP - Internet of Plants. Some of the documentation here is a compressed version of the already existing or future blog posts there.

## Introduction

Let's face it: I'm a poor gardener. My main problem: Forgetting to water the plants. I'm thinking about this problem for many years now. Many ideas came to my mind, but I didn't actually start something in this regard.

In 2017, sometime between spring and summer, I stumbled across the Gardena MicroDrip system. It's a starter-kit that uses a (indeed very very small) pump within some sort of water reservoir. The last part was pretty important, because my balcony has no direct water tap. The kit came with some pipe, some drippers, the water pump and a very basic controller. The controller is able to do some fixed-interval programs, but you can't freely program it.

## Goals

I'm aiming at more flexibility in regard to watering the plants. The basic idea is still to use fixed intervals, but drop waterings in case of rain or extend the watering time if the sun was shining heavily.

In the first step, I'm going to implement just the fixed interval watering. That's more or less what the simple Gardena controller can do, but with the addition of monitoring the water reservoir. This will allow me to receive alarms, when the water level is too low. Additionally, the pump won't run dry anymore as it does with the Gardena controller.

The second step will be adding more advanced logic to have make the watering more flexible. The basic idea will be to still to use fixed intervals, but drop waterings in case of rain or extend the watering time if the sun was shining heavily. I'm planning to address this step much later this year.

## Hardware platform

My hardware platform is based on an ESP32. I designed a base board with some relays, power supply, UART interfaces (for sensors, e.g. the fill level sensor for the reservoir) and the ESP-WROOM-32 module.

![Irrigation Controller main board](doc/irrigation_ctrl_article.jpg)

On the top edge of the board are connectors for power, the relay outputs and the UARTs. The connectors are screw terminals which can be disconnected without loosening the screws, which might be handy when installing the hardware in its IP67 enclosure.

The design has two switching regulators: The first one is the main regulator for the 3.3V I use to power the ESP32 and all other digital stuff on the board. The second regulator is used to power the relays and external sensors. This regulator is normally off and can be activated via a GPIO of the ESP32. The UART level-shifters are also held in standby by default and can be powered up by the ESP32 on demand. I did this to reduce the power consumption as much as possible. I plan on driving the whole system on solar power sometime in the future. That's also the reason why I connected an ADC channel to the power input for voltage monitoring.

The fill level sensor (which is another project on its own) will be connected to one of the UARTs. Measurements will be triggered via a simple command/response scheme. I decided to decouple the two projects to try out different approaches for measuring the fill level in the real world. With the use of a generic command interface it doesn't matter how the level is actually measured.

## Current state

Hardware-wise, most of the board is tested:

* The switching regulators are working. The second regulator can be switched on and off on demand.
* The ESP is flashable and executes code.
* The relays can be controlled by the ESP.
* The battery voltage can be measured.

I haven't tested the UART connections yet, but that's what I plan to to next (hardware-wise).

The software is also starting to take shape:

* A command console via the debug/programming UART is implemented for testing purposes.
* The fill level sensor protocol handler is implemented, but mostly untested.
* WiFi connects properly to my home WLAN. I'm planning to implement a WiFi manager class to encapsulated it better.
* An MQTT client connects to my local broker. This will be used later for status information and alerts. A big task is to implement an MQTT manager class to handle multi-threading properly.
* The real time clock of the ESP is keeping the time, when the ESP is in deep sleep to preserve power. The time system is able to sync with an SNTP server. But the time can also be set via the command console.
* A rough plan to implement the control logic for the fixed irrigation plan is written down as comments within the IrrigationController class.

The configuration of the WiFi and other services is currently hardcoded in the file include/networkConfig.h. The repository contains a template version of the file. I'm planning to change the configuration mechanism either by providing console commands or by implementing a webserver.

![Early debugging session on the command console](doc/irrigation_control_bringup_console.png)

The screenshot above shows an early debugging session using the command console. You can see that an MQTT connections as been established and the time has been synced with an SNTP server. I tested some GPIO functionality in the session.

## Compiling and running

The project is a plain ESP-IDF project. It provides a good way to include external components into the compile flow. They are either included as git submodules or simple sub directories in the 'components' dir.

ToBeDone: Document the following steps:

* git checkout with submodules
* setting ESP-IDF environment
* compile
* flash + run
* connect to debug console

## Third party software components

The following projects are used as additional components. Some of them are from third parties or forked versions of third party components/libraries:

* [console-esp32](https://github.com/mwick83/console-esp32): The command console. It is based on [Elecia White's command console](https://github.com/eleciawhite/reusable/) and was ported by me to the ESP32 and lives in my gitub repository. The original code was published in the public domain. Be sure to check out here embedded software engineering podcast [Embedded.fm](http://embedded.fm/). Thanks Elecia!
* [espmqtt](https://github.com/mwick83/espmqtt): The MQTT client library by Tuan PM. I forked it to modify its configuration mechanism. You can find the [original repo here](https://github.com/tuanpmt/espmqtt). It is licensed under the Apache-2.0 license.

The following components are implemented by me and have their own github repositories:

* [mqtt-manager-esp32](https://github.com/mwick83/mqtt-manager-esp32): The MQTT client manager used to handle multi-threading properly.

## License

Copyright (c) 2017-2018 Manuel Wick

Licensed under the BSD 3-clause "New" or "Revised" License.
See LICENSE.md file or [http://www.opensource.org/licenses/BSD-3-Clause](http://www.opensource.org/licenses/BSD-3-Clause) for details.
