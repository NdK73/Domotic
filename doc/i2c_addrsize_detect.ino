#include <Wire.h>

int dev=-1;

void setup() {
  Serial.begin(115200);
  Wire.begin();

  for(int i2c=0; i2c<8; ++i2c) {
    uint8_t error;
    uint8_t type=0, release=0;

    Wire.beginTransmission(0x50+i2c);
    if(Wire.endTransmission()) {        // EEPROM not found
      Serial.print("No dev @");
      Serial.println(i2c, DEC);
    } else {
      // EEPROM found, get board type and release
      Serial.print("Found dev @");
      Serial.println(i2c, DEC);
      if(-1==dev) dev=i2c;
    }
  }
  Serial.println("Commands:");
  Serial.println("r/R: read current device (addr 0-7)");
  Serial.println("w/W: write to current device");
  Serial.println("0-7: change current device");
  Serial.print("Current device is ");
  Serial.println(dev, DEC);
  Serial.println("d: test detection algorithm on current device");
}

void loop() {
  if(Serial.available()) {
    uint8_t header[8], cnt=0;
    char c=Serial.read();
    switch(c) {
      case 'r':
      case 'R':
        Serial.println(c);
        Wire.beginTransmission(0x50+dev);
        if('R'==c) Wire.write(0);
        Wire.write(0);
        Wire.endTransmission(false);
        Wire.requestFrom(0x50+dev, sizeof(header), (bool)false);
        while(Wire.available() && cnt<sizeof(header)) {
          header[cnt++]=Wire.read();
          Serial.print(header[cnt-1], HEX);
          Serial.print(" ");
        }
        Serial.println("");
        Wire.endTransmission(true);
        break;
      case 'w':
      case 'W':
        Serial.print("W");
        Wire.beginTransmission(0x50+dev);
        if('W'==c) Wire.write(0);
        Wire.write(0x0);
        Wire.write(0xd7); // Magic High
        Wire.write(0x4a); // Magic Low
        Wire.write(dev?0xfe:0x02); // Unknown (not @0) or DomoNode-Inputs (@0)
        Wire.write(0x00); // First release
        Wire.write(0xff); // Board ID (blank)
        Wire.write(0xff);
        Wire.write(0xff);
        Wire.write(0xff);
        Wire.endTransmission();
        break;
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
        dev=c-'0';
        Serial.print("Current device is ");
        Serial.println(dev, DEC);
        break;
      case '\n':
      case '\r':
        break;
      case 'd':
        uint8_t retry[8];
        Serial.println("Detect: first read");
        Wire.beginTransmission(0x50+dev);
        Wire.write(0);
        Wire.endTransmission(false);
        Wire.requestFrom(0x50+dev, sizeof(header), (bool)false);
        cnt=0;
        while(Wire.available() && cnt<sizeof(header)) {
          header[cnt++]=Wire.read();
          Serial.print(header[cnt-1], HEX);
          Serial.print(" ");
        }
        Serial.println("");
        Wire.endTransmission(true);

        Serial.println("Detect: second read");
        Wire.beginTransmission(0x50+dev);
        Wire.write(0);
        Wire.endTransmission(false);
        Wire.requestFrom(0x50+dev, sizeof(retry), (bool)false);
        cnt=0;
        while(Wire.available() && cnt<sizeof(retry)) {
          retry[cnt++]=Wire.read();
          Serial.print(retry[cnt-1], HEX);
          Serial.print(" ");
        }
        Serial.println("");
        Wire.endTransmission(true);

        if(!memcmp(header, retry, 8)) {
          Serial.println("The two reads match");
          if(0xd7==header[0] && 0x4a==header[1]) {
            Serial.println("8-bit address");
          } else {
            Serial.println("Probably 16-bit address");
            Wire.beginTransmission(0x50+dev);
            Wire.write(0); // 16-bit address
            Wire.write(0); // Would overwrite address 0 on 8-bit addressed device
            Wire.endTransmission(false);
            Wire.requestFrom(0x50+dev, sizeof(header), (bool)false);
            cnt=0;
            while(Wire.available() && cnt<sizeof(header)) {
              header[cnt++]=Wire.read();
              Serial.print(header[cnt-1], HEX);
              Serial.print(" ");
            }
            Serial.println("");
            Wire.endTransmission(true);
            if(0xd7==header[0] && 0x4a==header[1]) {
              Serial.println("16-bit address confirmed");
            } else {
              Serial.println("Uninitialized device?");
            }
          }
        }
        break;
      default:
        Serial.println("Unknown command");
    }
  }
}
