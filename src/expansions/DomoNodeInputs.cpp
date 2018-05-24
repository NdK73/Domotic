#include "DomoNodeInputs.h"

// Returns an instance of the (derived) class if it can handle type/release, else NULL
DomoNodeExpansion *DomoNodeInputs::getInstance(uint8_t typeCode, uint8_t release, uint8_t addr, void* opts)
{
  if(2==typeCode)
    return new DomoNodeInputs(addr);
  return NULL;
}

// Getters
bool DomoNodeInputs::din(int io)
{
  return 0;
}

// Specs
// These are used to fill AnswerSpec.
// Must only fill Info* part and description up to maxlen bytes
// Returns the used len in buff (terminating \0 must not be included in len)
int DomoNodeInputs::getDigitalInName(int i, char* buff, int maxlen)
{
  return 0;
}


// Set descriptions
// When possible set description in EEPROM.
// Returns number of characters written or 0 in case of error
int DomoNodeInputs::setDigitalInName(int i, const char *name)
{
  return 0;
}

DomoNodeInputs::DomoNodeInputs(uint8_t addr)
    : DomoNodeExpansion(addr)
{
}
