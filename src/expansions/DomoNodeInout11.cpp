#include "DomoNodeInout11.h"

// Returns an instance of the (derived) class if it can handle type/release, else NULL
DomoNodeExpansion *DomoNodeInout11::getInstance(const uint8_t header[], uint8_t addr, void* opts)
{
  if(1==header[2] && 1==header[3])
    return new DomoNodeInout11(addr);
  return NULL;
}

// Getters
bool DomoNodeInout11::din(int io)
{
  return false;
}

bool DomoNodeInout11::dout(int io)
{
  return false;
}

// Setters
bool DomoNodeInout11::dout(int io, bool val)
{
  return false;
}


// Specs
// These are used to fill AnswerSpec.
// Must only fill Info* part and description up to maxlen bytes
// Returns the used len in buff (terminating \0 must not be included in len)
int DomoNodeInout11::getDigitalInName(int i, char* buff, int maxlen)
{
  return 0;
}

int DomoNodeInout11::getDigitalOutName(int o, char* buff, int maxlen)
{
  return 0;
}


// Set descriptions
// When possible set description in EEPROM.
// Returns number of characters written or 0 in case of error
int DomoNodeInout11::setDigitalInName(int i, const char *name)
{
  return 0;
}

int DomoNodeInout11::setDigitalOutName(int o, const char *name)
{
  return 0;
}

// Can be constructed only via getInstance()
DomoNodeInout11::DomoNodeInout11(uint8_t addr)
    : DomoNodeExpansion(addr)
{
}

void DomoNodeInout11::handler()
{
#warning "TODO"
}
