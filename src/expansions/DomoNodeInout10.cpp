#include "DomoNodeInout10.h"
#include "Wire.h"

// DomoNode-Inout 1.0 had no EEPROM to store config, so values are hardcoded
// IO0.0 to IO0.3 are outputs
// IO0.4 is unconnected (WP in v1.1+)
// IO0.5 to IO0.7 are inputs
// IO1.x are unused (currently)

// Returns an instance of the (derived) class if it can handle type/release, else NULL
DomoNodeExpansion *DomoNodeInout10::getInstance(const uint8_t header[], uint8_t addr, void* opts)
{
    // Caller already checked PCA9555 presence (and absence of EEPROM)
    return new DomoNodeInout10(addr);
}

// Setters
bool DomoNodeInout10::dout(int io, bool val)
{
  if(0<=io && io<4) {
    if(val)
      _state |= 1<<io;
    else
      _state &= ~(1<<io);
    return true;
  }
  return false;
}

int DomoNodeInout10::getDigitalInName(int i, char* buff, int maxlen)
{
    if(i<0 || i>3 || !buff || maxlen<5) return 0;
    buff[0]='E';	// 'E'xpansion
    buff[1]='0'+_addr;
    buff[2]='H';	// 'H'igh
    buff[3]='V';	// 'V'oltage (Input)
    buff[4]='0'+i;
    buff[5]=0;
    return 5;
}

int DomoNodeInout10::getDigitalOutName(int o, char* buff, int maxlen)
{
    if(o<0 || o>4 || !buff || maxlen<5) return 0;
    buff[0]='E';	// 'E'xpansion
    buff[1]='0'+_addr;
    buff[2]='R';	// 'R'eLay
    buff[3]='L';	// ----^
    buff[4]='0'+o;
    buff[5]=0;
    return 5;
}

DomoNodeInout10::DomoNodeInout10(uint8_t addr)
  : DomoNodeExpansion(addr)
  , _state(0xFF)
{
  Wire.beginTransmission(0x20+_addr);
  Wire.write(0x06);  // pointer to Direction register 0
  Wire.write(0xF0);  // 0..3 as outputs
  Wire.write(0xFF);  // All the others are inputs
  Wire.endTransmission();
}

void DomoNodeInout10::handler()
{
  uint16_t t=0;
  // Update outputs
  Wire.beginTransmission(0x20+_addr);
  Wire.write(0x02);  // pointer to Output port 0
  Wire.write(_state & 0xFF);
  Wire.endTransmission(true);
  // Update inputs
  Wire.beginTransmission(0x20+_addr);
  Wire.write(0x00);  // pointer to Input registers
  Wire.endTransmission(false);
  Wire.requestFrom(static_cast<uint8_t>(0x20+_addr), static_cast<uint8_t>(2));
  t=Wire.read();
  t|=Wire.read()<<8;
  t|=_state&0x0F;	// Keep track of outputs
  _state=t;
}
