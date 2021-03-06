#include "DomoNodeExpansion.h"
// DomoNode-Inout 1.0 had no EEPROM to store config so values are hardcoded

class DomoNodeInout10 : public DomoNodeExpansion {
    public:
        // Returns an instance of the (derived) class if it can handle type/release, else NULL
        static DomoNodeExpansion *getInstance(const uint8_t header[], uint8_t addr, void* opts);

        // Counters
        virtual int ains() { return 0; };
        virtual int dins() { return 3; };
        virtual int aouts() { return 0; };
        virtual int douts() { return 4; };

        // Getters
        virtual int ain(int io) { return 0; };
        virtual bool din(int io) { if(0<=io && io<3) return (_state&(1<<(5+io))); return true; };
        virtual int aout(int io) { return 0; };
        virtual bool dout(int io) { if(0<=io && io<4) return (_state&(1<<io)); return true; };
        // Setters
        virtual int aout(int io, int val) { return 0; };
        virtual bool dout(int io, bool val);

        // Specs
        // These are used to fill AnswerSpec.
        // Must only fill Info* part and description up to maxlen bytes
        // Returns the used len in buff (terminating \0 must not be included in len)
        virtual int getAnalogInSpec(int i, char* buff, int maxlen) { return 0; };
        virtual int getDigitalInName(int i, char* buff, int maxlen);
        virtual int getAnalogOutSpec(int o, char* buff, int maxlen) { return 0; };
        virtual int getDigitalOutName(int o, char* buff, int maxlen);

        // Set descriptions
        // When possible set description in EEPROM.
        // Returns number of characters written or 0 in case of error
        // No EEPROM => no write capability
        virtual int setAnalogInName(int i, const char *name) { return 0; };
        virtual int setDigitalInName(int i, const char *name) { return 0; };
        virtual int setAnalogOutName(int o, const char *name) { return 0; };
        virtual int setDigitalOutName(int o, const char *name) { return 0; };
        // Fake ID: 0x1010000x x=address (0-7)
        virtual uint32_t getID() { return 0x10100000+_addr; };
        virtual void handler();

    protected:
        // Can be constructed only via getInstance()
        explicit DomoNodeInout10(uint8_t addr);

    private:
        uint16_t _state;
};
