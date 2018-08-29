#include "DomoNodeExpansion.h"

class DomoNodeInputs : public DomoNodeExpansion {
    public:
        // Returns an instance of the (derived) class if it can handle type/release, else NULL
        static DomoNodeExpansion *getInstance(const uint8_t header[], uint8_t addr, void* opts);

        // Counters
        virtual int ains() { return 0; };
        virtual int dins() { return 16; };
        virtual int aouts() { return 0; };
        virtual int douts() { return 0; };

        // Getters
        virtual int ain(int io) { return 0; };
        virtual bool din(int io) {
            if(0<=io && io<16)
                return _state & (1<<io);
            else
                return true;
        };
        virtual int aout(int io) { return 0; };
        virtual bool dout(int io) { return 0; };
        // Setters
        virtual int aout(int io, int val) { return 0; };
        virtual bool dout(int io, bool val) { return 0; };

        // Specs
        // These are used to fill AnswerSpec.
        // Must only fill Info* part and description up to maxlen bytes
        // Returns the used len in buff (terminating \0 must not be included in len)
        virtual int getDigitalInName(int i, char* buff, int maxlen);
        virtual int getDigitalOutName(int o, char* buff, int maxlen) { return 0; };
        virtual int getAnalogInSpec(int i, char* buff, int maxlen) { return 0; };
        virtual int getAnalogOutSpec(int o, char* buff, int maxlen) { return 0; };

        // Set descriptions
        // When possible set description in EEPROM.
        // Returns number of characters written or 0 in case of error
        virtual int setDigitalInName(int i, const char *name);
        virtual int setDigitalOutName(int o, const char *name) { return 0; };
        virtual int setAnalogInName(int i, const char *name) { return 0; };
        virtual int setAnalogOutName(int o, const char *name) { return 0; };

        virtual uint32_t getID() { return 0x10120000+_addr; };
        virtual void handler();

        const uint8_t MAX_NAME_LEN=15;

    protected:
        // Can be constructed only via getInstance()
        DomoNodeInputs(uint8_t addr);

    private:
        uint16_t _state;
};
