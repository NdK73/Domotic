#include "Domotic.h"
#include <Wire.h>
#include <WiFiUdp.h>

#include "expansions/DomoNodeExpansion.h"
#include "expansions/DomoNodeInout10.h"
#include "expansions/DomoNodeInout11.h"
#include "expansions/DomoNodeInputs.h"

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
  for(uint8_t addr=0; addr<Domotic::MAX_EXPS; ++addr) {
    _exps[addr]=NULL;
  }
}

Domotic::~Domotic() {
  delete _doutMap;
  delete _aoutMap;
  delete _dinMap;
  delete _ainMap;
  delete _text;
  for(uint8_t addr=0; addr<Domotic::MAX_EXPS; ++addr) {
    delete _exps[addr];
    _exps[addr]=NULL;
  }
}

void Domotic::begin(void) {
  if(_initialized)
    return;

  //Autodetect expansion boards & update in/out counter!
  Wire.begin(); // Just to be sure
  for(int i2c=0; i2c<Domotic::MAX_EXPS; ++i2c) {
    uint8_t error;
    uint8_t type=0, release=0;

    Wire.beginTransmission(0x50+i2c);
    if(Wire.endTransmission()) {	// EEPROM not found
      // PCA9555 is at 0x20-0x27
      Wire.beginTransmission(0x20+i2c);
      if(!Wire.endTransmission()) { // Found PCA9555 in DomoNode-inout 1.0 (no EEPROM)
        _exps[i2c]=DomoNodeInout10::getInstance(1, 0, i2c, NULL);
      }
    } else {
      uint8_t header[4], cnt=0;
      // EEPROM found, get board type and release
      Wire.beginTransmission(0x50+i2c);
      Wire.write(0);
      Wire.endTransmission(false);
      Wire.requestFrom((uint8_t)(0x50+i2c), sizeof(header), (bool)true);
      while(Wire.available() && cnt<sizeof(header)) {
        header[cnt++]=Wire.read();
      }
      // A proper factory pattern would only waste precious RAM
      // Every class "knows" its keys
      _exps[i2c]=DomoNodeInout11::getInstance(header[0], header[1], i2c, NULL);
      if(!_exps[i2c])
        _exps[i2c]=DomoNodeInputs::getInstance(header[0], header[1], i2c, NULL);
      // just "cascade" other expansions: the first matching one will be saved
      //if(!_exps[i2c])
      //  _exps[i2c]=DomoNodeInputs::getInstance(header[0], header[1], i2c, NULL);
    }
    if(_exps[i2c]) {
      _douts+=_exps[i2c]->douts();
      _aouts+=_exps[i2c]->aouts();
      _dins+=_exps[i2c]->dins();
      _ains+=_exps[i2c]->ains();
    }
  }

  _aoutMap=(uint16_t *)calloc(_aouts, sizeof(_aoutMap[0]));
  _doutMap=(uint16_t *)calloc(_douts, sizeof(_doutMap[0]));
  _ainMap=(uint16_t *)calloc(_ains, sizeof(_ainMap[0]));
  _dinMap=(uint16_t *)calloc(_dins, sizeof(_dinMap[0]));

  initMaps();

  // Setup networking
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
      if(_lastpkt[offset]=='S') {
        // Parse signature
        ++offset;
        verifySig(offset, len, true);
      }
      if(data-offset>=10 && _lastpkt[offset]=='U') {	// Regular update
        Domotic::UpdDir d;
        Domotic::UpdType t;
        uint16_t group=0;
        uint8_t b;
        offset=1;

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

        // Group
        if(hex2uint8(_lastpkt+offset, &b)) return;
        group=b;
        offset+=2;
        if(hex2uint8(_lastpkt+offset, &b)) return;
        group=(group<<8)+b;
        offset+=2;

        if(Domotic::TYPE_DIGITAL) {
          if('1'==_lastpkt[offset]) b=1;
          else if('0'==_lastpkt[offset]) b=0;
          else return;
        } else {
          b=0;
        }
        // Packet parsed OK, run callback
        processNotification(d, t, group, b, data-offset, offset);
      } else if(_lastpkt[offset]=='T' && data-offset>=12) { // Time update (usually signed)
        ++offset; // Skip 'T'
        uint8_t epoch;
        uint32_t tstamp;
        int8_t tz=0;
        bool dst=false;
        uint8_t b;

        if(hex2uint8(_lastpkt+offset, &epoch)) return;
        // epoch is currently fixed at 0
        if(0!=epoch) return;
        offset+=2;

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
    Process only the *Spec part (offset already points past 'C')
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
  char type=_lastpkt[offset++];

#warning "Could be a problem with delayed verification of signatures?"
  _lastpkt[0]='W'; // Overwrites received packet (only already-parsed part)
  _lastpkt[1]=type; // In unsecure packets simply overwites that byte with its current contents

  // Assuming all object types follow the same layout (type obj param) where only param changes for different types
  uint8_t obj;
  if(Domotic::hex2uint8(_lastpkt+offset, &obj)) {
    return DomError::ERR_CMD_BAD; // no hex chars where expected
  }
  offset+=2;

  switch(type) {
    case 'D': {
      bool v='1'==_lastpkt[offset];
      // Handle "toggle" state
      if('T'==_lastpkt[offset]) {
        readDigitalOut(obj, v); // Read current state
        v=!v; // Toggle it
      }
      _lastpkt[2]=v?'1':'0';
      offset=0; len=3; // Answer is ready... IF write succeeds

      return writeDigitalOut(obj, v);
    }; break;
    case 'A': {
      uint8_t b;
      uint16_t v=0;
      if(hex2uint8(_lastpkt+offset, &b)) return DomError::ERR_CMD_BAD; // no hex chars where expected
      v=uint16_t(b)<<8;
      offset+=2;
      if(hex2uint8(_lastpkt+offset, &b)) return DomError::ERR_CMD_BAD; // no hex chars where expected
      v+=b;
      return writeAnalogOut(obj, v);
    }; break;
    case 'R': {
      return DomError::ERR_UNSUPP;
#warning "incomplete!"
    }; break;
  }
  // Unsupported command
  return Domotic::DomError::ERR_CMD_UNS;
};

Domotic::DomError Domotic::writeDigitalOut(uint8_t obj, bool val)
{
  Domotic::DomError e=DomError::ERR_CMD_RANGE;
  if(_douts<obj) return e;

  if(obj<douts()) {
    // Accessing local output
    dout(obj, val);
    e=DomError::ERR_OK;
  } else {
    // Accessing expansion output
    obj-=douts(); // Skip local douts

    // Scan expansions
    for(uint8_t addr=0; addr<Domotic::MAX_EXPS; ++addr) {
      if(_exps[addr]) {
        if(obj<_exps[addr]->douts()) {
          _exps[addr]->dout(obj, val);
          e=Domotic::DomError::ERR_OK;
          break; // Exit for() loop
        }
        obj-=_exps[addr]->douts(); // obj was not in this expansion, try next one
      }
    }
  }
  return e;
}

Domotic::DomError Domotic::writeAnalogOut(uint8_t obj, uint16_t val)
{
  Domotic::DomError e=DomError::ERR_CMD_RANGE;
  if(_aouts<obj) return e;

  if(obj<aouts()) {
    // Accessing local output
    aout(obj, val);
    e=DomError::ERR_OK;
  } else {
    // Accessing expansion output
    obj-=aouts(); // Skip local aouts

    // Scan expansions
    for(uint8_t addr=0; addr<Domotic::MAX_EXPS; ++addr) {
      if(_exps[addr]) {
        if(obj<_exps[addr]->aouts()) {
          _exps[addr]->aout(obj, val);
          e=Domotic::DomError::ERR_OK;
          break; // Exit for() loop
        }
        obj-=_exps[addr]->aouts(); // obj was not in this expansion, try next one
      }
    }
  }
  return e;
};

Domotic::DomError Domotic::readAnalogIn(uint8_t obj, uint16_t &val)
{
  Domotic::DomError e=DomError::ERR_CMD_RANGE;
  if(_ains<obj) return e;

  if(obj<ains()) {
    // Accessing local lines
    val=ain(obj);
    e=DomError::ERR_OK;
  } else {
    // Accessing expansion lines
    obj-=ains(); // Skip local aouts

    // Scan expansions
    for(uint8_t addr=0; addr<Domotic::MAX_EXPS; ++addr) {
      if(_exps[addr]) {
        if(obj<_exps[addr]->ains()) {
          val=_exps[addr]->ain(obj);
          e=Domotic::DomError::ERR_OK;
          break; // Exit for() loop
        }
        obj-=_exps[addr]->ains(); // obj was not in this expansion, try next one
      }
    }
  }
  return e;
}

Domotic::DomError Domotic::readAnalogOut(uint8_t obj, uint16_t &val)
{
  Domotic::DomError e=DomError::ERR_CMD_RANGE;
  if(_aouts<obj) return e;

  if(obj<aouts()) {
    // Accessing local lines
    val=aout(obj);
    e=Domotic::DomError::ERR_OK;
  } else {
    // Accessing expansion lines
    obj-=aouts(); // Skip local aouts

    // Scan expansions
    for(uint8_t addr=0; addr<Domotic::MAX_EXPS; ++addr) {
      if(_exps[addr]) {
        if(obj<_exps[addr]->aouts()) {
          val=_exps[addr]->aout(obj);
          e=Domotic::DomError::ERR_OK;
          break; // Exit for() loop
        }
        obj-=_exps[addr]->aouts(); // obj was not in this expansion, try next one
      }
    }
  }
  return e;
}

Domotic::DomError Domotic::readDigitalOut(uint8_t obj, bool &val)
{
  Domotic::DomError e=DomError::ERR_CMD_RANGE;
  if(_douts<obj) return e;

  if(obj<douts()) {
    // Accessing local lines
    val=dout(obj);
    e=Domotic::DomError::ERR_OK;
  } else {
    // Accessing expansion lines
    uint8_t o=obj-douts();

    // Scan expansions
    for(uint8_t addr=0; addr<Domotic::MAX_EXPS; ++addr) {
      if(_exps[addr]) {
        if(obj<_exps[addr]->douts()) {
          val=_exps[addr]->dout(obj);
          e=Domotic::DomError::ERR_OK;
          break;
        }
        o-=_exps[addr]->douts();
      }
    }
  }
  return e;
}

Domotic::DomError Domotic::readDigitalIn(uint8_t obj, bool &val)
{
  Domotic::DomError e=DomError::ERR_CMD_RANGE;
  if(_ains<obj) return e;

  if(obj<dins()) {
    val=din(obj);
    e=Domotic::DomError::ERR_OK;
  } else {
    // Scan expansions
    uint8_t o=obj-dins();
    for(uint8_t addr=0; addr<Domotic::MAX_EXPS; ++addr) {
      if(_exps[addr]) {
        if(obj<_exps[addr]->dins()) {
          val=_exps[addr]->din(obj);
          e=Domotic::DomError::ERR_OK;
          break;
        }
        o-=_exps[addr]->dins();
      }
    }
  }
  return e;
}

Domotic::DomError Domotic::readAnalogOutSpec(uint8_t obj, int &len)
{
  Domotic::DomError e=DomError::ERR_CMD_RANGE;
  if(_aouts<obj) return e;

  char *out=(char *)_lastpkt+len;

  if(obj<aouts()) {
    // Accessing local lines
    len+=getAnalogOutSpec(obj, out, DOMOTIC_MAX_PKT_SIZE-len);
    e=Domotic::DomError::ERR_OK;
  } else {
    // Accessing expansion lines
    obj-=aouts(); // Skip local lines

    // Scan expansions
    for(uint8_t addr=0; addr<Domotic::MAX_EXPS; ++addr) {
      if(_exps[addr]) {
        if(obj<_exps[addr]->aouts()) {
          len+=_exps[addr]->getAnalogOutSpec(obj, out, DOMOTIC_MAX_PKT_SIZE-len);
          e=Domotic::DomError::ERR_OK;
          break; // Exit for() loop
        }
        obj-=_exps[addr]->aouts(); // obj was not in this expansion, try next one
      }
    }
  }
  return e;
}

Domotic::DomError Domotic::readAnalogInSpec(uint8_t obj, int &len)
{
  Domotic::DomError e=DomError::ERR_CMD_RANGE;
  if(_ains<obj) return e;

  char *out=(char *)_lastpkt+len;

  if(obj<ains()) {
    // Accessing local lines
    len+=getAnalogInSpec(obj, out, DOMOTIC_MAX_PKT_SIZE-len);
    e=Domotic::DomError::ERR_OK;
  } else {
    // Accessing expansion lines
    obj-=ains(); // Skip local lines

    // Scan expansions
    for(uint8_t addr=0; addr<Domotic::MAX_EXPS; ++addr) {
      if(_exps[addr]) {
        if(obj<_exps[addr]->ains()) {
          len+=_exps[addr]->getAnalogInSpec(obj, out, DOMOTIC_MAX_PKT_SIZE-len);
          e=Domotic::DomError::ERR_OK;
          break; // Exit for() loop
        }
        obj-=_exps[addr]->ains(); // obj was not in this expansion, try next one
      }
    }
  }
  return e;
}

Domotic::DomError Domotic::readDigitalOutSpec(uint8_t obj, int &len)
{
  Domotic::DomError e=DomError::ERR_CMD_RANGE;
  if(_douts<obj) return e;

  // Digital lines are always booleans
  _lastpkt[len++]='B';

  char *out=(char *)_lastpkt+len;

  if(obj<douts()) {
    // Accessing local lines
    len+=getDigitalOutName(obj, out, DOMOTIC_MAX_PKT_SIZE-len);

    e=Domotic::DomError::ERR_OK;
  } else {
    // Accessing expansion lines
    obj-=douts(); // Skip local lines

    // Scan expansions
    for(uint8_t addr=0; addr<Domotic::MAX_EXPS; ++addr) {
      if(_exps[addr]) {
        if(obj<_exps[addr]->douts()) {
          len+=_exps[addr]->getDigitalOutName(obj, out, DOMOTIC_MAX_PKT_SIZE-len);
          e=Domotic::DomError::ERR_OK;
          break; // Exit for() loop
        }
        obj-=_exps[addr]->douts(); // obj was not in this expansion, try next one
      }
    }
  }
  return e;
}

Domotic::DomError Domotic::readDigitalInSpec(uint8_t obj, int &len)
{
  Domotic::DomError e=DomError::ERR_CMD_RANGE;
  if(_dins<obj) return e;

  // Digital lines are always booleans
  _lastpkt[len++]='B';

  char *out=(char *)_lastpkt+len;

  if(obj<dins()) {
    // Accessing local lines
    len+=getDigitalInName(obj, out, DOMOTIC_MAX_PKT_SIZE-len);
    e=Domotic::DomError::ERR_OK;
  } else {
    // Accessing expansion lines
    obj-=dins(); // Skip local lines

    // Scan expansions
    for(uint8_t addr=0; addr<Domotic::MAX_EXPS; ++addr) {
      if(_exps[addr]) {
        if(obj<_exps[addr]->dins()) {
          len+=_exps[addr]->getDigitalInName(obj, out, DOMOTIC_MAX_PKT_SIZE-len);
          e=Domotic::DomError::ERR_OK;
          break; // Exit for() loop
        }
        obj-=_exps[addr]->dins(); // obj was not in this expansion, try next one
      }
    }
  }
  return e;
}

Domotic::DomError Domotic::processInfo(int &offset, int &len)
{
/*
    InfoPacket := <'I'> {DigitalReadSpec | AnalogReadSpec | InfoSpec | RegisterReadSpec}
      DigitalReadSpec := <'D'> <'I'|'O'> <io#:ByteHex>
      AnalogReadSpec := <'A'> <'I'|'O'> <io#:ByteHex>
      InfoSpec := <'I'> <'A'|'D'> <'I'|'O'> <io#:ByteHex>
      RegisterReadSpec := <'R'> <reg#:ByteHex> [<arrayelement#:ByteHex>]

The first character ('I') is already parsed, so offset is *at least* 1, but it could be bigger if the packet is encrypted and/or signed.

Answers:
  ID: <'R'> <'D'> <'0'|'1'>
  IA: <'R'> <'A'> <value#:WordHex>
  IR: <'R'> <'R'> {<'L'> <len:ByteHex> | <'V'> <0x20-0x7f>*}
  II: <'R'> <'I'> {InfoBool | InfoPercent | InfoTemp | InfoPower | InfoUserFloat | InfoText} <descr:<0x20-0x7f>*>
    InfoBool := <'B'>                                                   // Used for digital lines
    InfoPercent := <'%'> <decimals:0-3>
    InfoTemp := <'K'> <decimals:0-2>                                    // By default room temperatures are reported in centi-Kelvin (K2)
    InfoPower := <'W'> <decimals:0-2>                                   // Power in Watts (0.00 to 65535) -- usually home appliances use decimals=0 (1W resolution)
    InfoUserFloat := <'F'> <numerator:WordHex> <denominator:WordHex>    // Float=(numerator*regvalue)/(denominator*65536); note that result is strictly < numerator/denominator
    InfoText := <'T'> <maxlen:ByteHex>
*/

  char act=_lastpkt[offset++];
  char type, dir;
  DomError r=DomError::ERR_OK;
  uint8_t obj=0;

  _lastpkt[0]='R'; // Overwrites received packet (only already-parsed part)
  _lastpkt[1]=act; // In unsecure packets simply overwites that byte with its current contents

  len=2;

  switch(act) {
    case 'D': // Read digital line
      // Serial.println("IDdxx");
      dir=_lastpkt[offset++];
      if(Domotic::hex2uint8(_lastpkt+offset, &obj)) {
        return Domotic::DomError::ERR_INF_BAD; // no hex chars where expected
      }
      if(Domotic::UpdDirC::DIRC_IN==dir) {
        bool val;
        r=readDigitalIn(obj, val);
        if(DomError::ERR_OK==r) {
          _lastpkt[len++]=(val?'1':'0');
        }
      } else if(Domotic::UpdDirC::DIRC_OUT==dir) {
        bool val;
        r=readDigitalOut(obj, val);
        if(DomError::ERR_OK==r) {
          _lastpkt[len++]=(val?'1':'0');
        }
      } else {
        return DomError::ERR_INF_BAD;
      }
      break;
    case 'A': // Read analog line
      // Serial.println("IAdxx");
      dir=_lastpkt[offset++];
      if(Domotic::hex2uint8(_lastpkt+offset, &obj)) {
        return DomError::ERR_INF_BAD; // no hex chars where expected
      }

      if(Domotic::UpdDir::DIR_IN==dir) {
        uint16_t val;
        r=readAnalogIn(obj, val);
        if(DomError::ERR_OK==r) {
          len+=sprintf((char *)_lastpkt+len, "%04X", val);
        }
      } else if(Domotic::UpdDir::DIR_OUT==dir) {
        uint16_t val;
        r=readAnalogOut(obj, val);
        if(DomError::ERR_OK==r) {
          len+=sprintf((char *)_lastpkt+len, "%04X", val);
        }
      } else {
        return DomError::ERR_INF_BAD;
      }
      break;
    case 'I': // Read spec
      // Serial.println("IItdxx");

      type=_lastpkt[offset++];
      if(UpdTypeC::TYPEC_ANALOG!=type && UpdTypeC::TYPEC_DIGITAL!=type) {
        return DomError::ERR_INF_BAD; // unknown line type
      }
//      _lastpkt[len++]=type; // Not in the protocol! Decoding just requires Info*

      dir=_lastpkt[offset++];
      if((Domotic::UpdDirC::DIRC_IN!=dir) && (Domotic::UpdDirC::DIRC_OUT!=dir)) {
        return DomError::ERR_INF_BAD; // unknown line type
      }
      if(Domotic::hex2uint8(_lastpkt+offset, &obj)) {
        return DomError::ERR_INF_BAD; // no hex chars where expected
      }

      if(UpdTypeC::TYPEC_ANALOG==type) {
        if(Domotic::UpdDirC::DIRC_IN==dir) {
          // Serial.println("IIAIxx");
          r=readAnalogInSpec(obj, len);
        } else { // Cannot be anything else than 'O' (already checked)
          // Serial.println("IIAOxx");
          r=readAnalogOutSpec(obj, len);
        }
      } else { // No need to check: can *only* be 'D' (previously checked)
        if(Domotic::UpdDirC::DIRC_IN==dir) {
          // Serial.println("IIDIxx");
          r=readDigitalInSpec(obj, len);
        } else { // Cannot be anything else than 'O' (already checked)
          // Serial.println("IIDOxx");
          r=readDigitalOutSpec(obj, len);
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
          len+=sprintf((char *)(_lastpkt+len),
            PROTOVERSION " %d %d %d %d %d %c", _douts, _dins, _aouts, _ains, _tlen, _utf?'T':'F');
          break;
        case 0x01: // Node keys and supported algorithms
          {
          uint8_t r;
          if(Domotic::hex2uint8(_lastpkt+offset+2, &r)) { // Missing optional param: read array len
            _lastpkt[2]='L';
            len=3;
            len+=sprintf((char*)(_lastpkt+len), "%02X", 0); // @@@ TODO: get keyslot counter
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
              len+=sprintf((char*)_lastpkt+len, "%04X", _doutMap[r]);
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

bool Domotic::sigBuff(char* buff, uint16_t keyID)
{
  int len=strlen(buff);
  int slen=128;
#warning "@@@ TODO: get signature len from key type"

  if(DOMOTIC_MAX_PKT_SIZE<(1+4+slen+len))
    return true;

  memmove(buff+1+4+slen, buff, len);
  buff[0]=Domotic::DomPktType::PKT_SIG;
  sprintf(buff+1, "%04X", keyID);
  for(int t=0; t<slen; ++t) buff[1+4+t]='0'; // @@@ TODO

  return false;
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
  // Replace in-place hex-encoded signature with binary one
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

/*
 * Send a multicast notification of the changed IO state (notify()) or current time (notifyTime())
 * Does not (currently) modify _lastpkt
 *  UpdatePkt := <'U'> EventSpec
 *    EventSpec := InEvent | OutEvent | TimeEvent
 *      InEvent := <'I'> {DigitalEvent | AnalogEvent}
 *        DigitalEvent := <'D'> <group:WordHex> <'0'|'1'>
 *        AnalogEvent := <'A'> <group:WordHex> <value:WordHex>
 *      OutEvent := <'O'> {DigitalEvent | AnalogEvent}
 *      TimeEvent := <'T'> <epoch:ByteHex> <hictr:WordHex> <loctr:WordHex> <tz_dst:ByteHex>
 */

void Domotic::notify(Domotic::UpdDir d, Domotic::UpdType t, uint8_t num, uint16_t signKey)
{
  if(!_initialized)
    return;

  uint16_t *pGroup;
  char buff[DOMOTIC_MAX_PKT_SIZE];
  int pos=0;
  Domotic::DomError e=Domotic::DomError::ERR_OK;

  buff[pos++]=Domotic::DomPktType::PKT_UPD;
  buff[pos++]=d?'O':'I';

  if(t==Domotic::UpdType::TYPE_ANALOG) {
    // Analog IO
    buff[pos++]='A';
    uint16_t val=0;

    if(Domotic::UpdDir::DIR_IN==d) {
      e=readAnalogIn(num, val);
      pGroup=_ainMap; // Delay dereference after error check
    } else {
      e=readAnalogOut(num, val);
      pGroup=_aoutMap; // Delay dereference after error check
    }
    if(Domotic::DomError::ERR_OK!=e || !pGroup || 0==pGroup[num])
      return;

    sprintf(buff+pos, "%04X%04X",
      pGroup[num],
      val
      );
  } else {
    // Digital IO
    buff[pos++]='D';
    bool val;

    if(Domotic::UpdDir::DIR_IN==d) {
      e=readDigitalIn(num, val);
      pGroup=_dinMap; // Delay dereference after error check
    } else {
      e=readDigitalOut(num, val);
      pGroup=_doutMap; // Delay dereference after error check
    }
    if(Domotic::DomError::ERR_OK!=e || !pGroup || 0==pGroup[num])
      return;

    sprintf(buff+pos, "%04X%c",
      pGroup[num],
      val?'1':'0'
      );
  }

  if(0xffff!=signKey) {
    // Create actual signature
    if(sigBuff(buff, signKey))
      return;
  }

  _udp->beginPacketMulticast(_mcastAddr, _port, WiFi.localIP());
  _udp->println((const char *)buff);
  _udp->endPacket();

}

void Domotic::notifyTime(uint8_t epoch, uint32_t counter, uint8_t tz, uint16_t signKey)
{
  if(!_initialized)
    return;

  char buff[DOMOTIC_MAX_PKT_SIZE];

  sprintf(buff, "%c%02X%08X%02X",
    Domotic::DomPktType::PKT_UPD,
    epoch,
    counter,
    tz
    );

  if(0xffff!=signKey) {
    // Create actual signature
    if(sigBuff(buff, signKey))
      return;
  }

  _udp->beginPacketMulticast(_mcastAddr, _port, WiFi.localIP());
  _udp->println((const char *)buff);
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
