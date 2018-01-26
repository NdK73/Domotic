#include "Domotic.h"
#include <WiFiUdp.h>

#define PROTOVERSION "0.1.0"

Domotic::Domotic()
: _port(DOMOTIC_DEF_UDP_PORT)
, _udp(0)
, _initialized(false)
, _mcastAddr(DOMOTIC_DEF_UDP_MCAST)
, _douts(0)
, _aouts(0)
, _dins(0)
, _ains(0)
, _tlen(0)
, _utf(false)
, _doutMap(NULL)
, _aoutMap(NULL)
, _dinMap(NULL)
, _ainMap(NULL)
, _text(NULL)
, _isSigned(false)
, _signKey(0)
{
}

Domotic::~Domotic() {
  delete _doutMap;
  delete _aoutMap;
  delete _dinMap;
  delete _ainMap;
  delete _text;
}

void Domotic::begin(void) {
  if(_initialized)
    return;

  _udp = new WiFiUDP();
  if(!_udp)
    return;
  // _udp->begin(_port); // included in beginMulticast
  _udp->beginMulticast(WiFi.localIP(), _mcastAddr, _port);

  _initialized=true;
}

void Domotic::handle()
{
  handleNet(); // Always call network processing first!
  handler(); // Call derived class' method
}

void Domotic::handleNet()
{
  if(!_initialized)
    return;

  _isSigned=false;

  int data=_udp->parsePacket();
  if(data) {
    long rcv=millis();	// Timestamp packet arrival
    int len=_udp->read(_lastpkt, DOMOTIC_MAX_PKT_SIZE); // Read UP TO DOMOTIC_MAX_PKT_SIZE
    if(len && _lastpkt[len-1]=='\n')
      --len;  // remove stray \n in query
    _lastpkt[len]=0; // Make sure string terminates
    int offset=0;

    Domotic::DomError err=Domotic::ERR_UNKNOWN;

    if(_udp->destinationIP()==_mcastAddr) {
      // Process multicast data
//Serial.printf("mcast1: '%s'\n", (char*)_lastpkt);
      if(_lastpkt[offset]=='S') {
        // Parse signature
        ++offset;
        verifySig(offset, len, true);
      }
//Serial.printf("mcast2: '%s'\n", (char*)_lastpkt+offset);
      if(data-offset>=10 && _lastpkt[offset]=='U') {	// Regular update
        int sNode=0;
        Domotic::UpdDir d;
        Domotic::UpdType t;
        uint8_t b;
        offset=1;

        // Read 4 characters as sender nodeId
        if(2<hex2uint8(_lastpkt+offset, &b)) return;
        sNode=b;
        offset+=2;
        if(2<hex2uint8(_lastpkt+offset, &b)) return;
        sNode=(sNode<<8)+b;
        offset+=2;

        // Input or output?
        if(_lastpkt[offset]=='I') {
          d=Domotic::DIR_IN;
        } else
        if(_lastpkt[offset]=='O') {
          d=Domotic::DIR_OUT;
        } else
          return;
        ++offset;

        // Analog or digital?
        if(_lastpkt[offset]=='A') {
          t=Domotic::TYPE_ANALOG;
        } else
        if(_lastpkt[offset]=='D') {
          t=Domotic::TYPE_DIGITAL;
        } else
          return;

        // Channel
        if(2<hex2uint8(_lastpkt+offset, &b)) return;

        // Packet parsed OK, run callback
        processNotification(sNode, d, t, b, data-offset, offset);
      } else if(_lastpkt[offset]=='T' && data-offset>=12) { // Time update (usually signed)
        ++offset; // Skip 'T'
        uint8_t epoch;
        uint32_t tstamp;
        int8_t tz=0;
        bool dst=false;
        uint8_t b;

        if(2<hex2uint8(_lastpkt+offset, &epoch)) return;
        // epoch is currently fixed at 0
        if(0!=epoch) return;
        offset+=2;
//Serial.printf("epoch=%d\n", epoch);

        if(hex2uint8(_lastpkt+offset, &b)) return;
        tstamp=b;
        offset+=2;
        if(hex2uint8(_lastpkt+offset, &b)) return;
        tstamp=(tstamp<<8)+b;
        offset+=2;
        if(hex2uint8(_lastpkt+offset, &b)) return;
        tstamp=(tstamp<<8)+b;
        offset+=2;
        if(hex2uint8(_lastpkt+offset, &b)) return;
        tstamp=(tstamp<<8)+b;
        offset+=2;

        if(2<hex2uint8(_lastpkt+offset, &b)) return;
        tz=b&0x1F | ((b&0x10)?0xF0:0); // theoretically from -16 to +15, actually from -12 to +12
        dst=b&0x20;
        offset+=2;
//Serial.printf("tz=%d, dst=%c\n", tz, dst?'1':'0');

        tstamp+=(millis()-rcv+500)/1000; // verifying Ed25519 sig takes ~900ms, round it to 1s
        processTimeUpdate(epoch, tstamp, tz, dst);
      }
    } else {
      // Parse unicast packet
//Serial.printf("Req: '%s' from ", (char*)_lastpkt);
//Serial.println(_udp->remoteIP());

      if(_lastpkt[offset]==PKT_ENC) {
         err=Domotic::ERR_UNSUPP; // TODO
      }
      // Signature could be inside an encrypted packet
      if(_lastpkt[offset]==PKT_SIG) {
        verifySig(offset, len, false);	// Check immediately: parser modifies buffer for answer!
      }
      // SimplePkt can be encrypted and/or signed
      switch(_lastpkt[offset]) {
        case Domotic::DomPktType::PKT_CMD:
          ++offset; --len;
          err=processCommand(offset, len);
          break;
        case Domotic::DomPktType::PKT_INF:
          ++offset; --len;
          err=processInfo(offset, len);
          break;
        case Domotic::PKT_ANS:
        case Domotic::PKT_UPD:
        case Domotic::PKT_ENC:
        case Domotic::PKT_SIG:
          err=Domotic::ERR_CTX;
          len=0;
          break;
      }
  //Serial.printf("Ans: '%s'\n", (char*)_lastpkt+offset);
      // Send answer
      answer(err, len, offset);
    }
  }
}

Domotic::DomError Domotic::processCommand(int &offset, int &len)
{
/*
    CommandPacket := <'C'> {DigitalOutSpec | AnalogOutSpec | RegisterSpec}
      DigitalOutSpec := <'D'> <output#:ByteHex> <'0'|'1'|'T'>
      AnalogOutSpec := <'A'> <input#:ByteHex> <value:WordHex>
        WordHex := <hibyte:ByteHex> <lobyte:ByteHex>
      RegisterSpec := <'R'> <reg#:ByteHex> <0x20-0x7f>* // Till end of line or end of packet; register-specific parsing required
Answers:
  CD: <'W'> <'D'> <'0'|'1'>
  CA: <'W'> <'A'>
  CR: <'W'> <'R'>
*/
  offset=0; len=0; return Domotic::DomError::ERR_CMD_UNS;
/* @@@ TODO Calls:
    virtual DomError writeDigitalOut(uint8_t dout, bool value) {return DomError::ERR_CMD_BAD;};
    virtual DomError writeAnalogOut(uint8_t aout, uint16_t value) {return DomError::ERR_CMD_BAD;};
    virtual DomError writeRegister(uint8_t reg, int offset, int len) {return DomError::ERR_CMD_BAD;};
*/
};

Domotic::DomError Domotic::processInfo(int &offset, int &len)
{
/*
    InfoPacket := <'I'> {DigitalReadSpec | AnalogReadSpec | AnalogInfoSpec | RegisterReadSpec}
      DigitalReadSpec := <'D'> <'I'|'O'> <io#:ByteHex>
      AnalogReadSpec := <'A'> <'I'|'O'> <io#:ByteHex>
      InfoSpec := <'I'> <'A'|'D'> <'I'|'O'> <io#:ByteHex>
      RegisterReadSpec := <'R'> <reg#:ByteHex> [<arrayelement#:ByteHex>]

The first character ('I') is already parsed, so offset is *at least* 1, but it could be bigger if the packet is encrypted and/or signed.

Answers:
  IID: <'R'> <'D'> <'0'|'1'>
  IIA: <'R'> <'A'> <value#:WordHex>
  III: <'R'> <'I'> {InfoBool | InfoPercent | InfoTemp | InfoPower | InfoUserFloat | InfoText} <descr:<0x20-0x7f>*>
    InfoBool := <'B'>
    InfoPercent := <'%'> <decimals:0-3>
    InfoTemp := <'K'> <decimals:0-2>					// By default room temperatures are reported in centi-Kelvin (K2)
    InfoPower := <'W'> <decimals:0-2>					// Power in Watts (0.00 to 65535) -- usually home appliances use decimals=0 (1W resolution)
    InfoUserFloat := <'F'> <numerator:WordHex> <denominator:WordHex>	// Float=(numerator*regvalue)/(denominator*65536); note that result is strictly < numerator/denominator
    InfoText := <'T'> <maxlen:ByteHex>
  IIR: <'R'> <'R'> {<'L'> <len:ByteHex> | <'V'> <0x20-0x7f>*}
*/
  char act=_lastpkt[offset];
  ++offset; // Parsed "*Spec" part, in 'act'

  uint8_t obj=0;

  _lastpkt[0]='R'; // Overwrites received packet (only already-parsed part)
  _lastpkt[1]=act; // In unsecure packets simply overwites that byte with its current contents

  switch(act) {
    case 'D': // Read digital line
      // Serial.println("IDxx");
      if(Domotic::hex2uint8(_lastpkt+offset, &obj)) {
        return DomError::ERR_INF_BAD; // no hex chars where expected
      }
      _lastpkt[2]=_lastpkt[offset];
      len=3;

      if('I'==_lastpkt[2]) {
        if(_dins<obj) return DomError::ERR_INF_RANGE; else readDigitalIn(obj, offset, len);
      } else if('O'==_lastpkt[2]) {
        if(_douts<obj) return DomError::ERR_INF_RANGE; else readDigitalOut(obj, offset, len);
      } else {
        return DomError::ERR_INF_BAD;
      }
      break;
    case 'A': // Read analog line
      // Serial.println("IAxx");
      if(Domotic::hex2uint8(_lastpkt+offset, &obj)) {
        return DomError::ERR_INF_BAD; // no hex chars where expected
      }
      _lastpkt[2]=_lastpkt[offset];
      len=3;

      if('I'==_lastpkt[2]) {
        if(_ains<obj) return DomError::ERR_INF_RANGE; else readAnalogIn(obj, offset, len);
      } else if('O'==_lastpkt[2]) {
        if(_aouts<obj) return DomError::ERR_INF_RANGE; else readAnalogOut(obj, offset, len);
      } else {
        return DomError::ERR_INF_BAD;
      }
      break;
    case 'I': // Read spec
      // Serial.println("II.xx");
      if(_lastpkt[offset]=='A') {
        _lastpkt[2]='A';
      } else if(_lastpkt[offset]=='D') {
        _lastpkt[2]='D';
      } else
        return DomError::ERR_INF_BAD; // unknown line type
      ++offset;
      if(Domotic::hex2uint8(_lastpkt+offset+1, &obj)) {
        return DomError::ERR_INF_BAD; // no hex chars where expected
      }
      _lastpkt[3]=_lastpkt[offset];
      len=4;

      if(_lastpkt[2]=='A') {
        if('I'==_lastpkt[3]) {
          // Serial.println("IIAIxx");
          if(_ains<obj) return DomError::ERR_INF_RANGE; else readAnalogInSpec(obj, len);
        } else if('O'==_lastpkt[3]) {
          // Serial.println("IIAOxx");
          if(_aouts<obj) return DomError::ERR_INF_RANGE; else readAnalogOutSpec(obj, len);
        } else {
          return DomError::ERR_INF_BAD;
        }
      } else { // No need to check: can *only* be 'D' (previously checked)
        if('I'==_lastpkt[3]) {
          // Serial.println("IIDIxx");
          if(_dins<obj) return DomError::ERR_INF_RANGE; else readDigitalInSpec(obj, len);
        } else if('O'==_lastpkt[3]) {
          // Serial.println("IIDOxx");
          if(_douts<obj) return DomError::ERR_INF_RANGE; else readDigitalOutSpec(obj, len);
        } else {
          return DomError::ERR_INF_BAD;
        }
      }
      break;
    case 'R': // Read register
      if(Domotic::hex2uint8(_lastpkt+offset, &obj)) {
        return Domotic::DomError::ERR_INF_BAD; // no hex chars where expected
      }
      switch(obj) {
        case 0x00: // Version & node info
          _lastpkt[2]='V';
          len=3;
          len+=sprintf((char *)(_lastpkt+offset),
            PROTOVERSION " %d %d %d %d %d %c", _douts, _dins, _aouts, _ains, _tlen, _utf?'T':'F');
          break;
        case 0x01: // Node keys and supported algorithms
          {
          uint8_t r;
          if(Domotic::hex2uint8(_lastpkt+offset+2, &r)) { // Missing optional param: read array len
            _lastpkt[2]='L';
            len=3;
            len+=sprintf((char*)(_lastpkt+offset), "%02X", 0); // @@@ TODO: get keyslot counter
          } else {
            _lastpkt[2]='V';
            len=3;
            return ERR_INF_RANGE; // @@@ TODO: get keyslot info
          }
          }
          break;
        case 0x02: // Hostname
          _lastpkt[2]='V';
          len=3;
          len+=sprintf((char *)(_lastpkt+len), "%.32s (%s %s)", WiFi.hostname().c_str(), __DATE__, __TIME__);
          break;
        case 0x03: // Flags (RESERVED)
          _lastpkt[2]='V';
          len=3;
          len+=sprintf((char *)(_lastpkt+len), "%04X", 0x0000);
          break;
        case 0x04: // network info, string, write: "WIFI:SSID,password[,ip,mask]" (encrypted); read: "WIFI:SSID,ip"; write: "ETH:DHCP|ip,mask", read: "ETH:ip"; write/read: "BUS:nodeID" (bus must be on a different port than config interface)
          _lastpkt[2]='V';
          len=3;
          len+=sprintf((char *)(_lastpkt+len), "WIFI:%s,%s", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
          break;
        case 0x05: // display message, string, max max_txt_len (from R0) printable-ASCII characters (0x20-0x7e) for message
          _lastpkt[2]='V';
          len=3;
          //len+=sprintf((char *)(_lastpkt+offset), "%s", ???); // @@@ TODO
          break;
        case 0x20: // digital out port map
          {
          uint8_t r;
          if(Domotic::hex2uint8(_lastpkt+offset+2, &r)) { // Missing optional param: read array len
            _lastpkt[2]='L';
            len=3;
            len+=sprintf((char*)(_lastpkt+len), "%02X", _douts);
          } else {
            if(r<_douts) {
              _lastpkt[2]='V';
              len=3;
              len+=sprintf((char*)(_lastpkt+len), "%04X", _doutMap[r]);
            } else {
              return DomError::ERR_INF_RANGE;
            }
          }
          }
          break;
        case 0x21: // analog out port map
          {
          uint8_t r;
          if(Domotic::hex2uint8(_lastpkt+offset+2, &r)) { // Missing optional param: read array len
            _lastpkt[2]='L';
            len=3;
            len+=sprintf((char*)(_lastpkt+len), "%02X", _aouts);
          } else {
            if(r<_aouts) {
              _lastpkt[2]='V';
              len=3;
              len+=sprintf((char*)(_lastpkt+len), "%04X", _aoutMap[r]);
            } else {
              return DomError::ERR_INF_RANGE;
            }
          }
          }
          break;
        case 0x22: // digital in port map
          {
          uint8_t r;
          if(Domotic::hex2uint8(_lastpkt+offset+2, &r)) { // Missing optional param: read array len
            _lastpkt[2]='L';
            len=3;
            len+=sprintf((char*)(_lastpkt+len), "%02X", _dins);
          } else {
            if(r<_dins) {
              _lastpkt[2]='V';
              len=3;
              len+=sprintf((char*)(_lastpkt+len), "%04X", _dinMap[r]);
            } else {
              return DomError::ERR_INF_RANGE;
            }
          }
          }
          break;
        case 0x23: // analog in port map
          {
          uint8_t r;
          if(Domotic::hex2uint8(_lastpkt+offset+2, &r)) { // Missing optional param: read array len
            _lastpkt[2]='L';
            len=3;
            len+=sprintf((char*)(_lastpkt+len), "%02X", _ains);
          } else {
            if(r<_ains) {
              _lastpkt[2]='V';
              len=3;
              len+=sprintf((char*)(_lastpkt+len), "%04X", _ainMap[r]);
            } else {
              return DomError::ERR_INF_RANGE;
            }
          }
          }
          break;
/*
        case 0x2: //
          {
          uint8_t r;
          if(Domotic::hex2uint8(_lastpkt+offset+2, &r)) { // Missing optional param: read array len
            _lastpkt[2]='L';
            len=3;
            len+=sprintf((char*)(_lastpkt+len), "%02X", _douts);
          } else {
            if(r<_douts) {
              _lastpkt[2]='V';
              len=3;
              len+=sprintf((char*)(_lastpkt+len), "%04X", _doutMap[r]);
            } else {
              return DomError::ERR_INF_RANGE;
            }
          }
          }
          break;
24: (Array<Event>) events
25: (Array<?>) timer actions TODO
26: (Array<WordHex>) timers intervals; most significant nibble is: 0=Monostable|1=Astable, 00=milliseconds, 01=seconds, 10=minutes, 11=hours, 0=RESERVED; the remaining 12 bits define the actual interval
*/
        default:
          len=0;
          return ERR_INF_RANGE;
      }
      break;
    default:
      len=0;
      return ERR_INF_RANGE;
  }
  _lastpkt[len]=0; // terminate string
  offset=0; // All answers start at the beginning of _lastpkt
  return ERR_OK;
}

void Domotic::verifySig(int &offset, int len, bool fast)
{
  //Serial.printf("Verify (%s)\n", fast?"FAST":"full");
  uint8_t t;
  int off=offset;

  _isSigned=false;

  if(offset) {	// Fresh verification
    //Serial.println(" Fresh");
    // Parse _signKey
    uint16_t tk=_signKey=0;
    if(hex2uint8(_lastpkt+off, &t)) return; // Abort parsing
    tk=t<<8;
    off+=2;
    if(hex2uint8(_lastpkt+off, &t)) return; // Abort parsing
    tk|=t;
    off+=2;
    _signKey=tk;

    if(fast) {
      _signOffset=off; // Save where actual signature starts (no need to parse again _signKey)
    }
  } else {	// Reuse saved offset
    //Serial.println(" Redo");
    if(fast) return; // Useless to cycle again: already verified!
    off=_signOffset; // Rewind offset to perform a proper check
    // Now offset points to start of signature
  }

  // @@@ TODO: locate key from signKey
  const uint8_t pubkey[]={
    0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7,
    0xd5, 0x4b, 0xfe, 0xd3, 0xc9, 0x64, 0x07, 0x3a,
    0x0e, 0xe1, 0x72, 0xf3, 0xda, 0xa6, 0x23, 0x25,
    0xaf, 0x02, 0x1a, 0x68, 0xf7, 0x07, 0x51, 0x1a
    };
  int sigLen=64;

  uint8_t signature[sigLen];

  //Serial.printf(" Given len=%d\n", len);
  // calculate len
  if(0==len) {
    len=off;
    while(_lastpkt[len])
      ++len;
      //Serial.printf(" Calculated len=%d\n", len);
  }

  int cnt;
  // Replace hex-encoded signature with binary one
  for(cnt=0; cnt<sigLen && off<len; ++cnt, off+=2) {
    if(hex2uint8(_lastpkt+off, signature+cnt)) break;
  }
  // Proceed only if the signature was complete
  if(sigLen==cnt) {
    if(fast) {
      //Serial.println(" End of fast check: sign formally OK");
      // Packet is not considered signed, yet, but offset must be updated and _signKey is set
      offset=off;
      return;
    }
    bool rv=Ed25519::verify(signature, pubkey, _lastpkt+off, len-off);
    if(!rv) {
      //Serial.println(" SIG_BAD!");
      _signKey=0;
      _signOffset=0;
    } else {
      //Serial.println(" SIG_OK");
      offset=off;
      _isSigned=true;
    }
  } else { // Wrong sig len
    //Serial.println(" Wrong sig len");
    // Do not update offset, clear _signkey and _signOffset
    _signKey=0;
    _signOffset=0;
    return;
  }
}

// Send unicast answer to a request
void Domotic::answer(Domotic::DomError err, size_t size, int offset)
{
  if(!_initialized)
    return;

  uint8_t ecode=static_cast<uint8_t>(err);
  _udp->beginPacket(_udp->remoteIP(), _udp->remotePort());
  _udp->write(Domotic::DomPktType::PKT_ANS);
  _udp->print(ecode>>4, HEX);
  _udp->print(ecode&0xf, HEX);
  if(Domotic::DomError::ERR_OK==err) {
    // Append details only for "OK" answer
    _udp->write(_lastpkt+offset, size);
  }
  _udp->write((uint8_t)0);
  _udp->endPacket();
}

// Send a multicast notification of the changed state
void Domotic::notify(Domotic::UpdDir d, Domotic::UpdType t, uint8_t num, const char *txt)
{
  if(!_initialized)
    return;

  _udp->beginPacketMulticast(_mcastAddr, _port, WiFi.localIP());
  _udp->printf("%c%04X%c%c%02X",
    Domotic::DomPktType::PKT_UPD,
    _nodeID,
    d?'O':'I',
    t?'D':'A',
    num
    );
  if(txt)
    _udp->print(txt);
  _udp->write('\n');
  _udp->endPacket();
}

void Domotic::stop(void)
{
  if(!_initialized)
    return;
  _udp->stop();
}

// Takes up to 2 characters 0-9A-Fa-f and sets 'out' if *both* characters are valid.
// Returns 0 if both charaters got parsed, or the number of missing characters
int Domotic::hex2uint8(uint8_t *buff, uint8_t *out) {
    short idx;
    uint8_t rv=0;
    if(!buff || !out)  // both pointers are mandatory
      return 0;
    for(idx=0; idx<2; ++idx) {
      if(buff[idx]>='0' && buff[idx]<='9') {
          rv=(rv<<4)+(buff[idx]-'0');
      } else if(buff[idx]>='A' && buff[idx]<='F') {
          rv=(rv<<4)+(buff[idx]-'A'+10);
      } else if(buff[idx]>='a' && buff[idx]<='f') {
          rv=(rv<<4)+(buff[idx]-'a'+10);
      } else
          break;
    }
    if(2==idx) {
      *out=rv;
    }
    return 2-idx;
}
