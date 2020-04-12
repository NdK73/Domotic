/********************************
 * Base class for domotic protocol handling
 *
 * When implementing a sketch that uses Domotic protocol:
 * - derive your class from Domotic
 * - override the methods from ains() to handler() as needed
 * - instantiate your class (global scope)
 * - call Wire.begin(sda, scl) *before* calling yourclass.begin()
 * - call yourclass.begin() from setup()
 * - call yourclass.handle() from loop()
 * - manage *all* inputs from yourclass.handler() calling Domotic::notify() when needed
 *
 * To auto-learn a node (very minimal!):
 * - Send IR00 to determine presence and number of lines
 * - for each line (nn) :
 *   - send IR2tnn to read its mapping
 *   - send IItdnn to read its name (t={A|D},d={I|O})
 */

#pragma once

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

// Max MAX_PKT_SIZE is 1472 : bigger packets are not received by ESP, but we don't need such a monster
// Smaller packets are better for limited-resources devices and 256 is already stretching some limits
// (but a signed packet is 1+4+128+x bytes so can't reduce too much).
// Remember that *received* pkt can be 3 bytes longer ('Aee' where ee is error code)
#define DOMOTIC_MAX_PKT_SIZE 256
// Default port ("NdK" from base64-charset [13, 29, 10] to a 16-bit int)
#define DOMOTIC_DEF_UDP_PORT 55114
/*
 * Broadcast address used is 239.255.215.74 (RFC2365 "The IPv4 Local Scope", the last
 * two octects are 55114 -- see the notes about DEF_UDP_PORT)
 */
#define DOMOTIC_DEF_UDP_MCAST 239,255,215,74

#include "DomoticCrypto.h"
#include "expansions/DomoticIODescr.h"
#include "expansions/DomoNodeExpansion.h"

class Domotic : public DomoticIODescr
{
  public:
    enum DomError : uint8_t {
      ERR_OK = 0,       // No error
      ERR_CTX,          // Bad context (unencrypted packet for something that needed encryption? or multicast over unicast?)
      // 0x80-0x8f: COMMAND errors
      ERR_CMD_BAD=0x80, // Malformed/unrecognized command
      ERR_CMD_UNS,      // Unsupported command
      ERR_CMD_RANGE,    // Command referenced an unsupported index or attempted to set a value out of allowed range
      ERR_CMD_SIZE,     // Command tried to set a string to a value exceeding its limits
      // 0x90-0x9f: INFO errors
      ERR_INF_BAD=0x90, // Malformed/unrecognized Info request
      ERR_INF_RANGE,    // Unsupported index
      // 0xf0-fd: reserved
      ERR_UNSUPP=0xfe,  // Unsupported operation
      ERR_UNKNOWN=0xff, // Unspecified error
    };

    Domotic();
    ~Domotic();

    void disableScan(); // Do *not* scan I2C bus for expansions (default is to scan)
    void begin();
    void handle();

    // ****************** Setup methods ******************
    void setPort(int port) { if(!_initialized) _port=port; };
    void setMcast(IPAddress a) { if(!_initialized) _mcastAddr=a; };
    void stop(void);

    // ****************** Helper methods ******************

    // Parse 2 hex characters pointed by buff and sets out to the parsed value if both chars are valid;
    // returns 0 if both charaters got parsed, or the number of missing characters
    static int hex2uint8(uint8_t *buff, uint8_t *out);

    // Convert a float representing a temperature to/from centi-Kelvin
    static uint16_t temp2net(float temp) { return 27316+(int)(temp*100); };
    static float net2temp(uint16_t temp) { return temp/100.0 - 273.16; };

  protected:
    enum DomPktType : char {
      PKT_ANS = 'A',  // Answer packet (can *not* appear in multicast packet)
      PKT_CMD = 'C',  // Command (write)
      PKT_ENC = 'E',  // Encrypted Packet (contains signed/command/request)
      PKT_INF = 'I',  // Request info (read)
      PKT_SIG = 'S',  // Signed packet (contains command/request) -- different format for unicast / multicast
      PKT_UPD = 'U',  // Multicast update (can *not* appear in unicast packet)
    };

    enum UpdDirC : char {
      DIRC_IN  = 'I',
      DIRC_OUT = 'O'
    };

    enum UpdDir : bool {
      DIR_IN = false,
      DIR_OUT = true
    };

    enum UpdTypeC : char {
      TYPEC_ANALOG = 'A',
      TYPEC_DIGITAL= 'D'
    };

    enum UpdType : bool {
      TYPE_ANALOG = false,
      TYPE_DIGITAL= true
    };

    // This block should always be overridden (at least partially)
    // DomoticIODescr interface
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
    virtual int getAnalogInSpec(int i, char* buff, int maxlen) override { return 0; };  // Returns number of characters written
    virtual int getAnalogOutSpec(int o, char* buff, int maxlen) override { return 0; }; // Returns number of characters written
    virtual int getDigitalInName(int i, char* buff, int maxlen) override { return 0; }; // Returns number of characters written
    virtual int getDigitalOutName(int o, char* buff, int maxlen) override { return 0; };// Returns number of characters written
    virtual int setDigitalInName(int i, const char *name) override { return 0; };       // Returns number of characters written
    virtual int setDigitalOutName(int o, const char *name) override { return 0; };      // Returns number of characters written
    virtual int setAnalogInName(int i, const char *name) override { return 0; };        // Returns number of characters written
    virtual int setAnalogOutName(int o, const char *name) override { return 0; };       // Returns number of characters written

    virtual void initMaps() {}; // Called by begin() to initialize IO mapping data (arrays are already allocated and initialized to 0)
    virtual void handler() {}; // Called by handle() to process application-specific logic in derived class and notify changes

    // No return: multicast packets don't send answers
    virtual void processNotification(UpdDir d, UpdType t, int group, uint16_t val, size_t size, int offset=0) {};
    virtual void processTimeUpdate(uint8_t epoch, uint32_t timestamp, int8_t tz, bool dst) {};

    // Probably the following methods will never need overrides
    // ********************************************************

    int recvPkt(); // Called by handleNet(); returns amount of available new data in _lastpkt

    // Callbacks receive the offset in _lastpkt to start parsing from, for up to 'len' bytes.
    // If present, encrypted packets are decrypted and signed ones are verified) *before* callback.
    // Returns an error code and (iff err is 00) offset and len of a string (starting at _lastpkt) to be appended to answer
    // Can overwrite _lastpkt at will up to DOMOTIC_MAX_PKT_SIZE.
    // These are quite low-level and usually should *not* be redefined
    virtual DomError processCommand(int &offset, int &len);
    virtual DomError processInfo(int &offset, int &len);

    // Dispatchers for operations: these methods convert from "absolute" IO to device+io and call appropriate (overridden) method
    // Read methods write answer (only the value) in _lastpkt+offset (at most 'len' bytes) or in the passed object 'val'
    virtual DomError writeDigitalOut(uint8_t dout, bool value);
    virtual DomError writeAnalogOut(uint8_t aout, uint16_t value);
    virtual DomError readDigitalOut(uint8_t dout, bool& val);
    virtual DomError readAnalogOut(uint8_t aout, uint16_t &val);
    virtual DomError readDigitalIn(uint8_t ain, bool &val);
    virtual DomError readAnalogIn(uint8_t ain, uint16_t &val);

    // These are the low-level versions of get*Spec() methods from DomoticIODescr
    // Result must be placed in _lastpkt+len, then len must be updated to the len of the full string
    virtual DomError readAnalogOutSpec(uint8_t aout, int &len);		// Variable-len output
    virtual DomError readAnalogInSpec(uint8_t ain, int &len);		// Variable-len output
    virtual DomError readDigitalOutSpec(uint8_t dout, int &len);	// Variable-len output
    virtual DomError readDigitalInSpec(uint8_t din, int &len);		// Variable-len output

    // Send an answer to current packet (unicast)
    void answer(DomError err, size_t size, int offset=0); // Answer with 'size' bytes from _lastpkt+offset

    // Send a notification (multicast)
    void notify(UpdDir d, UpdType t, uint8_t num, uint16_t signKey=0xFFFF);
    void notifyTime(uint8_t epoch, uint32_t counter, uint8_t tz, uint16_t signKey=0xFFFF);

    // Crypto ops

    // verify signature starting at _lastpkt+offset (if offset>0) or at _lastpkt+_signOffset if offset is 0
    // At exit verified message starts at _lastpkt+offset and ends at _lastpkt+len
    // Modifies _lastpkt content with binary data and updates offset
    // If fast is true, then no pk crypto is performed -- notifiee can then choose to ask for signature check after inspecting packet contents (f.e. if time skew is too big)
    void verifySig(int &offset, int len, bool fast);

    // Signs 0-terminated buffer contents using keyID
    // Modifies buff to include signature and protocol markers, shifting current content as needed
    // Returns true in case of error (unknown keyID, buffer too small, ...)
    bool sigBuff(char* buff, uint16_t keyID);

    // Copy from src in PROGMEM to _lastpkt+pos
    // Returns number of copied bytes
    int pStr2lastpkt(int pos, PGM_P src)
      { _lastpkt[DOMOTIC_MAX_PKT_SIZE-1]=0; return strlen(strncpy_P((char*)_lastpkt+pos, src, DOMOTIC_MAX_PKT_SIZE-pos-1)); };

    // Base64 (urlsafe charset) encoding/decoding
    // Operates in-place on data in _lastpkt -- be sure to leave enough space for encoding!
    bool b64enc(int &from, size_t len);	// Overwrites _lastpkt[from] and following bytes; returns true if space gets exhausted; if 0==from, updates it with the space needed to encode len bytes
    bool b64dec(int &from, size_t len);	// Decodes len bytes from 'from', overwriting 'em while processing; returns true in case of error

  protected:
    // State
    int _port;
    WiFiUDP *_udp;
    bool _initialized;
    uint8_t _lastpkt[DOMOTIC_MAX_PKT_SIZE+4];	// Account for A00 and terminator in answers
    IPAddress _mcastAddr;
    uint8_t _douts, _aouts, _dins, _ains, _tlen; // Total, for base + all detected extensions
    bool _utf;
    uint16_t *_doutMap, *_aoutMap, *_dinMap, *_ainMap, *_text;

    // Signature handling
    bool _isSigned;	// True iff packet contains a valid signature
    uint16_t _signKey;	// keyid of signing key if _isSigned, else 0
    int _signOffset;	// offset of (decoded-to-binary) signature is saved here
    int _signData;	// offset of signed data is saved here

  private:
    static const int MAX_EXPS=8;
    static const int EXPANSION_MARKER=0xD74A;
    static const int EXPANSION_HDRSIZE=16;
    bool _doNotScan;	// Set by disableScan()
    DomoNodeExpansion *_exps[MAX_EXPS];
    void handleNet();
    // Obey the rule-of-three: Domotic must not be copied
    Domotic(const Domotic &src) = delete;
    Domotic &operator=(const Domotic &src) = delete;
};
