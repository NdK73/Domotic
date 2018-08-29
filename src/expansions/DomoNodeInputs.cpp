#include "DomoNodeInputs.h"
#include "Wire.h"

// Returns an instance of the (derived) class if it can handle type/release, else NULL
DomoNodeExpansion *DomoNodeInputs::getInstance(const uint8_t header[], uint8_t addr, void* opts)
{
  if(2==header[2])
    return new DomoNodeInputs(addr);
  return NULL;
}

// Specs
// These are used to fill AnswerSpec.
// Must only fill Info* part and description up to maxlen bytes
// Returns the used len in buff (terminating \0 must not be included in len)
int DomoNodeInputs::getDigitalInName(int i, char* buff, int maxlen)
{
  if(i<0 || i>15 || !buff || !maxlen)
    return 0;

  uint8_t addr=DomoNodeExpansion::HEADER_SIZE+i*MAX_NAME_LEN;
  Wire.beginTransmission(0x50+_addr);
  Wire.write(addr);
  Wire.endTransmission(false);
  Wire.requestFrom(static_cast<uint8_t>(0x20+_addr), MAX_NAME_LEN);
  int l=0;
  while(l<=MAX_NAME_LEN && l<maxlen && (buff[l]=Wire.read())) {
    ++l;
  }
  return l; // Must not include \0 in len
}

// Set descriptions
// When possible set description in EEPROM.
// Returns number of characters written or 0 in case of error
int DomoNodeInputs::setDigitalInName(int i, const char *name)
{
  if(i<0 || i>15 || !name || !name[0])
    return 0;

  uint8_t addr=DomoNodeExpansion::HEADER_SIZE+i*15;
  Wire.beginTransmission(0x50+_addr);
  Wire.write(addr);
  int l=0;
  while(l<=MAX_NAME_LEN && name[l]) {
    Wire.write(name[l]);
    ++l;
  }
  Wire.endTransmission(true);
  return l-1; // Must not include \0 in len
}

DomoNodeInputs::DomoNodeInputs(uint8_t addr)
    : DomoNodeExpansion(addr)
{
  // All lines are inputs
  Wire.beginTransmission(0x20+_addr);
  Wire.write(0x06);  // pointer
  Wire.write(0xFF);  // DDR Port0 all inputs 
  Wire.write(0xFF);  // DDR Port1 all inputs
  Wire.endTransmission(true);
}

void DomoNodeInputs::handler()
{
  uint16_t t=0;
  Wire.beginTransmission(0x20+_addr);
  Wire.write(0x00);  // pointer
  Wire.endTransmission(false);
  Wire.requestFrom(static_cast<uint8_t>(0x20+_addr), static_cast<uint8_t>(2));
  t=Wire.read()<<8;
  t|=Wire.read();
  _state=t;
}
