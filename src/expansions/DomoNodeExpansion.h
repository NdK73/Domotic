/*
 * Base class for extension boards drivers
 *
 * TypeIDs stored in EEPROM are:
 * 0 Reserved/Unknown
 * 1 DomoNodeInout (release 0 is DomoNodeInout10 and release 1 is DomoNodeInout11)
 * 2 DomoNodeInputs
*/
#if !defined(_DOMONODEEXPANSION)
#define _DOMONODEEXPANSION
#include <stddef.h>
#include <stdint.h>
#include "DomoticIODescr.h"

class DomoNodeExpansion : public DomoticIODescr {
    public:
        // Returns an instance of the (derived) class if it can handle type/release, else NULL
        static DomoNodeExpansion *getInstance(uint8_t type, uint8_t release, uint8_t addr, void* opts);

        virtual int ains() { return 0; };
        virtual int dins() { return 0; };
        virtual int aouts() { return 0; };
        virtual int douts() { return 0; };
        virtual int ain(int i) { return 0; };
        virtual bool din(int i) { return false; };
        virtual int aout(int o) { return 0; };
        virtual bool dout(int o) { return false; };
        virtual int aout(int o, int val) { return 0; };
        virtual bool dout(int o, bool val) { return false; };
        virtual int getDigitalInName(int i, char* buff, int maxlen) { return 0; };
        virtual int getDigitalOutName(int o, char* buff, int maxlen) { return 0; };
        virtual int getAnalogInSpec(int i, char* buff, int maxlen) { return 0; };
        virtual int getAnalogOutSpec(int o, char* buff, int maxlen) { return 0; };
        virtual int setDigitalInName(int i, const char *name) { return 0; };
        virtual int setDigitalOutName(int o, const char *name) { return 0; };
        virtual int setAnalogInName(int i, const char *name) { return 0; };
        virtual int setAnalogOutName(int o, const char *name) { return 0; };

    protected:
        uint8_t _addr;
        // Can be constructed only via getInstance()
        DomoNodeExpansion(int addr)
            : _addr(addr)
        {};
};

#endif
