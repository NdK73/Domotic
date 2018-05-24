#include "DomoNodeExpansion.h"
// DomoNode-Inout 1.0 had no EEPROM to store config so values are hardcoded

class DomoNodeInout10 : public DomoNodeExpansion {
    public:
        // Returns an instance of the (derived) class if it can handle type/release, else NULL
        static DomoNodeExpansion *getInstance(uint8_t typeCode, uint8_t release, uint8_t addr, void* opts);

        // Counters
        virtual int ains() { return 0; };
        virtual int dins() { return 3; };
        virtual int aouts() { return 0; };
        virtual int douts() { return 4; };

        // Getters
        virtual int ain(int io) { return 0; };
        virtual bool din(int io);
        virtual int aout(int io) { return 0; };
        virtual bool dout(int io);
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

    protected:
        // Can be constructed only via getInstance()
        DomoNodeInout10(uint8_t addr);
};
