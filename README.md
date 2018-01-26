# Domotic
An implementation of protocol described in [Home Automation](https://github.com/NdK73/HomeAutomation) repository

**Still subject to modifications -- ALPHA code**: class interface will be stabilized when the protocol is completely defined (v1.0.0).

crypto/ directory contains selected files from [ArduinoLibs](https://github.com/rweather/arduinolibs) .

Currently assumes ESP8266 nodes (w/ WiFi). I know I'll have to decouple transport from protocol decoding.

To use it, include Domotic.h then derive a class from Domotic. The derived class **should** override all the relevant methods.
