#include "DomoNodeInout10.h"

// DomoNode-Inout 1.0 had no EEPROM to store config, so values are hardcoded

// Returns an instance of the (derived) class if it can handle type/release, else NULL
DomoNodeExpansion *DomoNodeInout10::getInstance(const uint8_t header[], uint8_t addr, void* opts)
{
    // Caller already checked PCA9555 presence (and absence of EEPROM)
    return new DomoNodeInout10(addr);
}

// Getters
bool DomoNodeInout10::din(int io)
{
#warning "Should read"
  return false;
}

bool DomoNodeInout10::dout(int io)
{
#warning "Should read"
  return false;
}

// Setters
bool DomoNodeInout10::dout(int io, bool val)
{
#warning "Should set"
  return false;
}

int DomoNodeInout10::getDigitalInName(int i, char* buff, int maxlen)
{
    if(i<0 || i>3 || !buff || maxlen<5) return 0;
    buff[0]='E';
    buff[1]='0'+_addr;
    buff[2]='H';
    buff[3]='V';
    buff[4]='0'+i;
    buff[5]=0;
    return 5;
}

int DomoNodeInout10::getDigitalOutName(int o, char* buff, int maxlen)
{
    if(o<0 || o>4 || !buff || maxlen<5) return 0;
    buff[0]='E';
    buff[1]='0'+_addr;
    buff[2]='R';
    buff[3]='L';
    buff[4]='0'+o;
    buff[5]=0;
    return 5;
}

DomoNodeInout10::DomoNodeInout10(uint8_t addr)
    : DomoNodeExpansion(addr)
{
}

void DomoNodeInout10::handler()
{
#warning "TODO"
}
