/*
 * Methods and definitions for crypto-related ops in Domotic lib
 * Selected files from https://github.com/rweather/arduinolibs/
 * except RNGClass that have been rewritten to wrap ESP8266's TRNG
 * Some ideas mediated from NaCl https://nacl.cr.yp.to/
 * TODO: check https://github.com/jedisct1/libhydrogen
*/
#ifndef _DOMOTICCRYPTO_H
#define _DOMOTICCRYPTO_H

#include <stddef.h>
#include <stdint.h>
#include "RNG.h"

// Base class for all the keyslots (key instances)
// must be initialized by calling initialize()
class KeySlot {
  public:
    union cypherCaps {
      struct {
        uint32_t crypt:1;
        uint32_t decrypt:1;
        uint32_t sign:1;
        uint32_t verify:1;
        uint32_t keyexch: 1;
      };
      uint32_t intval;
    };

    // Initialize actual key material
    // If blob is NULL, key is initialized from RNGClass (useful for asymmetric keys).
    static KeySlot initialize(uint16_t id, const uint8_t *blob, const int blen)=0;

    // Return the cypher name
    virtual const char *getDescr() = 0;
    // Return cypher capabilities
    virtual const union cypherCaps getCaps() { union cypherCaps x; x.intval=0; return x; };
    virtual uint16_t getID() { return 0; };
    // Encrypt/decrypt buffer from src to src+slen in buffer at dst. Sets dlen (>=slen)
    // If dst is NULL, then this method only sets the desired dlen (so the caller can allocate dst)
    // If src and dst overlap result is undefined!
    // If adlen is nonzero, data pointed by ad is added to the message as authenticated plaintext.
    // If unencrypted (but authenticated) data is present in src buffer for decrypt(), its len is returned in adlen
    // Returns false if all is OK
    virtual bool encrypt(uint8_t *dst, int *dlen, const uint8_t *src, const int slen, uint8_t *ad=NULL, int adlen=0) { return true; };
    virtual bool decrypt(uint8_t *dst, int *dlen, int *adlen, const uint8_t *src, const int slen) { return true; };

    virtual bool sign(const uint8_t *src, const int slen, uint8_t *dst, int *dlen) { return true; };
    // Same as above, but does not overwrite sig (dst). Always sets sigLen
    // if fast is true, only a format check is performed, no actual crypto. If format is OK, returns false
    virtual bool verify(const uint8_t *src, const int slen, const uint8_t *sig, int *sigLen, bool fast) { return true; };

/*
    // Key exchange only uses local secret key, local public key and peer's public key
    // The initiator sends the first (and only) packet that is then used by target to decrypt the rest of the message
    virtual bool kexInitiator() =0;
    virtual bool kexTarget() =0;
*/
    ~KeySlot();
  protected:
    KeySlot();
  private:
};

/*
class Ed25519Full: public KeySlot
{
  public:
    Ed25519
}

class Ed25519VerifyOnly: public Key
{
  public:
    Ed25519
}
*/

extern RNGClass RNG;

#endif
