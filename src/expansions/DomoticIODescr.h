#if !defined _DOMOTICIODESCR
#define _DOMOTICIODESCR

// Interface methods used by Domotic and DomoNodeExpansion classes.
class DomoticIODescr
{
  public:
    // Counters
    virtual int ains() = 0;
    virtual int dins() = 0;
    virtual int aouts() = 0;
    virtual int douts() = 0;

    // Getters
    virtual int ain(int i) = 0;
    virtual bool din(int i) = 0;
    virtual int aout(int o) = 0;
    virtual bool dout(int o) = 0;
    // Setters
    virtual int aout(int o, int val) = 0;
    virtual bool dout(int o, bool val) = 0;

    // Specs
    // These are used to fill AnswerSpec.
    // Must only fill Info* part and description up to maxlen bytes
    // Returns the used len in buff (terminating \0 must not be included in len)
    virtual int getAnalogInSpec(int i, char* buff, int maxlen) = 0;
    virtual int getAnalogOutSpec(int o, char* buff, int maxlen) = 0;
    virtual int getDigitalInName(int i, char* buff, int maxlen) = 0;
    virtual int getDigitalOutName(int o, char* buff, int maxlen) = 0;

    // Set descriptions
    // When possible set description in EEPROM.
    // Returns number of characters written or 0 in case of error
    virtual int setDigitalInName(int i, const char *name) = 0;
    virtual int setDigitalOutName(int o, const char *name) = 0;
    virtual int setAnalogInName(int i, const char *name) = 0;
    virtual int setAnalogOutName(int o, const char *name) = 0;

    // Check and notify() changes in inputs
    virtual void handler() = 0;
};

#endif
