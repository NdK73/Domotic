/*
 * Base class for extension boards drivers
 *
 * TypeIDs stored in EEPROM are:
 * 0 Reserved/Unknown
 * 1 DomoNodeInout (release 0 is DomoNodeInout10 and release 1 is DomoNodeInout11)
 * 2 DomoNodeInputs
*/
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "DomoticIODescr.h"

class DomoNodeExpansion : public DomoticIODescr {
    public:
        // Returns an instance of the (derived) class if it can handle type/release, else NULL
        static DomoNodeExpansion *getInstance(const uint8_t header[], uint8_t addr, void* opts);

        virtual int ains() override { return 0; };
        virtual int dins() override { return 0; };
        virtual int aouts() override { return 0; };
        virtual int douts() override { return 0; };
        virtual int ain(int i) override { return 0; };
        virtual bool din(int i) override { return false; };
        virtual int aout(int o) override { return 0; };
        virtual bool dout(int o) override { return false; };
        virtual int aout(int o, int val) override { return 0; };
        virtual bool dout(int o, bool val) override { return false; };
        virtual int getDigitalInName(int i, char* buff, int maxlen) override { return 0; };
        virtual int getDigitalOutName(int o, char* buff, int maxlen) override { return 0; };
        virtual int getAnalogInSpec(int i, char* buff, int maxlen) override { return 0; };
        virtual int getAnalogOutSpec(int o, char* buff, int maxlen) override { return 0; };
        virtual int setDigitalInName(int i, const char *name) override { return 0; };
        virtual int setDigitalOutName(int o, const char *name) override { return 0; };
        virtual int setAnalogInName(int i, const char *name) override { return 0; };
        virtual int setAnalogOutName(int o, const char *name) override { return 0; };

        // Return the extension "unique" ID
        virtual uint32_t getID() = 0;

        //virtual void handler() = 0;

        const uint8_t HEADER_SIZE=16; // Size of reserved bytes in EEPROM

        virtual ~DomoNodeExpansion() = 0;

    protected:
        uint8_t _addr;
        // Can be constructed only via getInstance()
        explicit DomoNodeExpansion(int addr)
            : _addr(addr)
        {};
};

inline DomoNodeExpansion::~DomoNodeExpansion() {};
