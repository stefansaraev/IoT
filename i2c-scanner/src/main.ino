/*
  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

  Simple i2c scanner. Teted with ESP8266 + BH1750FVI
*/

#include <Arduino.h>
#include <Wire.h>

static int PIN_SDA = 2;
static int PIN_SCL = 4;

void i2c_scanner()
{
  byte error, address;
  int nDevices;
  Serial.println("Scanning...");

  nDevices = 0;
  for(address = 1; address < 127; address++ )
  {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    switch (error)
    {
      case 0:
        Serial.print("I2C device found at address 0x");
        if (address < 16)
          Serial.print("0");
        Serial.print(address,HEX);
        Serial.println("");
        nDevices++;
        break;
      case 4:
        Serial.print("Unknow error at address 0x");
        if (address<16)
          Serial.print("0");
        Serial.println(address,HEX);
        break;
    }
    delay(10);
  }

  if (nDevices == 0)
    Serial.println("No I2C devices found\n");
  else
    Serial.println("...done\n");
}

void setup()
{
  Serial.begin(115200);
  Serial.println("\n\n");
  Wire.begin(PIN_SDA, PIN_SCL);
}

void loop()
{
  i2c_scanner();
  delay(5000);
}
