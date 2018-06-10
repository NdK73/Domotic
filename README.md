# Domotic
An implementation of protocol described in [Home Automation](https://github.com/NdK73/HomeAutomation) repository

**Still subject to modifications -- ALPHA code**: class interface will be stabilized when the protocol is completely defined (v1.0.0).

crypto/ directory contains selected files from [ArduinoLibs](https://github.com/rweather/arduinolibs) .

Currently assumes ESP8266 nodes (w/ WiFi). I know I'll have to decouple transport from protocol decoding.

To use it, include Domotic.h then derive a class from Domotic. The derived class **should** override all the relevant methods.
The derived class' constructor must *NOT* initialize _ains, _aouts, _dins, _douts, members of base class: they're handled automatically.
_tlen and _utf must be initialized as appropriate. The actual values will automatically be updated during init() if expansions are detected.
Declare (in global scope)
DerivedClass myInstance;
instead of the (wrong) "Derivedclass myInstance = DerivedClass();" .
