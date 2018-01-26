#ifndef __DOMOTIC_H
#define __DOMOTIC_H

/********************************
 * Base class for domotic protocol handling
 */
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

class Domotic
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

    // Copy from src in PROGMEM to _lastpkt+pos
    // Returns number of copied bytes
    int pStr2lastpkt(int pos, PGM_P src)
      { _lastpkt[DOMOTIC_MAX_PKT_SIZE-1]=0; return strlen(strncpy_P((char*)_lastpkt+pos, src, DOMOTIC_MAX_PKT_SIZE-pos-1)); };

  protected:
    enum DomPktType : char {
      PKT_ANS = 'A',  // Answer packet (can *not* appear in unicast packet)
      PKT_CMD = 'C',  // Command (write)
      PKT_ENC = 'E',  // Encrypted Packet (contains signed/command/request)
      PKT_INF = 'I',  // Request info (read)
      PKT_SIG = 'S',  // Signed packet (contains command/request) -- different format for unicast / multicast
      PKT_UPD = 'U',  // Multicast update (can *not* appear in unicast packet)
    };

    enum UpdDir : bool {
      DIR_IN = false,
      DIR_OUT = true
    };

    enum UpdType : bool {
      TYPE_ANALOG = false,
      TYPE_DIGITAL= true
    };

    int recvPkt(); // Called by handleNet(); returns amount of available new data in _lastpkt
    virtual void handler() {}; // Called by handle() to process application-specific logic in derived class

    // Callbacks receive the offset in _lastpkt to start parsing from, for up to 'len' bytes.
    // If present, encrypted packets are decrypted and signed ones are verified) *before* callback.
    // Returns an error code and (iff err is 00) offset and len of a string (starting at _lastpkt) to be appended to answer
    // Can overwrite _lastpkt at will up to DOMOTIC_MAX_PKT_SIZE.
    // These are quite low-level and usually should *not* be redefined
    virtual DomError processCommand(int &offset, int &len) final;
    virtual DomError processInfo(int &offset, int &len) final;

    // Higher level callbacks, to be overridden, parameters range already verified
    // Read methods write answer (only the value) in _lastpkt+offset (at most 'len' bytes)
    virtual DomError writeDigitalOut(uint8_t dout, bool value) {return DomError::ERR_CMD_BAD;};
    virtual DomError writeAnalogOut(uint8_t aout, uint16_t value) {return DomError::ERR_CMD_BAD;};
    virtual DomError writeRegister(uint8_t reg, int &offset, int len) {return DomError::ERR_CMD_BAD;};
    virtual DomError readDigitalOut(uint8_t dout, uint16_t& val) {return DomError::ERR_INF_BAD; };
    virtual DomError readDigitalOut(uint8_t dout, int &offset, int &len) {	// Writes 1 byte ('0' or '1')
      uint16_t val;
      DomError r=readDigitalOut(dout, val);
      if(DomError::ERR_OK==r) {
        len+=sprintf((char *)_lastpkt+offset, "%c", val?'1':'0');
      }
      return r;
     };
    virtual DomError readAnalogOut(uint8_t aout, uint16_t &val) {return DomError::ERR_INF_BAD; };
    virtual DomError readAnalogOut(uint8_t aout, int &offset, int &len) {	// Writes 4 bytes (WordHex)
      uint16_t val;
      DomError r=readAnalogOut(aout, val);
      if(DomError::ERR_OK==r) {
        len+=sprintf((char *)_lastpkt+offset, "%04X", val);
      }
      return r;
     };
    virtual DomError readDigitalIn(uint8_t ain, uint16_t &val) {return DomError::ERR_INF_BAD; };
    virtual DomError readDigitalIn(uint8_t din, int &offset, int &len) {	// Writes 1 byte ('0' or '1')
      uint16_t val;
      DomError r=readDigitalIn(din, val);
      if(DomError::ERR_OK==r) {
        len+=sprintf((char *)_lastpkt+offset, "%04X", val);
      }
      return r;
     };
    virtual DomError readAnalogIn(uint8_t ain, uint16_t &val) {return DomError::ERR_INF_BAD; };
    virtual DomError readAnalogIn(uint8_t ain, int &offset, int &len) {		// Writes 4 byte (WordHex)
      uint16_t val;
      DomError r=readAnalogIn(ain, val);
      if(DomError::ERR_OK==r) {
        len+=sprintf((char *)_lastpkt+offset, "%04X", val);
      }
      return r;
     };

    // These must be redefined in derived class to describe (in human readable format) the IO lines
    // Result must be placed in _lastpkt+len, then len must be updated to the len of the full string
    virtual DomError readAnalogOutSpec(uint8_t aout, int &len) {return DomError::ERR_INF_BAD; };	// Variable-len output
    virtual DomError readAnalogInSpec(uint8_t ain, int &len) {return DomError::ERR_INF_BAD; };	// Variable-len output
    virtual DomError readDigitalOutSpec(uint8_t aout, int &len) {return DomError::ERR_INF_BAD; };	// Variable-len output
    virtual DomError readDigitalInSpec(uint8_t ain, int &len) {return DomError::ERR_INF_BAD; };	// Variable-len output
// Registers are managed internally, no need to expose an API
//    virtual DomError readRegister(uint8_t reg, int &offset, int &len) {return DomError::ERR_INF_BAD; };		// Access a plain (not array) register
//    virtual DomError readRegister(uint8_t reg, uint8_t elem, int &offset, int &len) {return DomError::ERR_INF_BAD; };	// Access an element of an array register

    // No return: multicast packets don't send answers
    virtual void processNotification(int node, UpdDir d, UpdType t, uint8_t num, size_t size, int offset=0) {};
    virtual void processTimeUpdate(uint8_t epoch, uint32_t timestamp, int8_t tz, bool dst) {};

    // Send an answer
    void answer(DomError err, size_t size, int offset=0); // Answer with 'size' bytes from _lastpkt+offset
    // Send a notification
    void notify(UpdDir d, UpdType t, uint8_t num, const char *txt);

    // Crypto ops

    // verify signature starting at _lastpkt+offset (if offset>0) or at _lastpkt+_signOffset if offset is 0
    // At exit verified message starts at _lastpkt+offset and ends at _lastpkt+len
    // Modifies _lastpkt content with binary data and updates offset
    // If fast is true, then no pk crypto is performed -- notifiee can then choose to ask for signature check after inspecting packet contents (f.e. if time skew is too big)
    void verifySig(int &offset, int len, bool fast);

    // State
    int _port;
    WiFiUDP *_udp;
    bool _initialized;
    uint8_t _lastpkt[DOMOTIC_MAX_PKT_SIZE+4];	// Account for A00 and terminator in answers
    IPAddress _mcastAddr;
    int _nodeID;
    uint8_t _douts, _aouts, _dins, _ains, _tlen;
    bool _utf;
    uint16_t *_doutMap, *_aoutMap, *_dinMap, *_ainMap, *_text;

    // Signature handling
    bool _isSigned;	// True iff packet contains a valid signature
    uint16_t _signKey;	// keyid of signing key if _isSigned, else 0
    int _signOffset;	// if fast verification, offset of signature is saved here

  private:
    void handleNet();
};

#endif
