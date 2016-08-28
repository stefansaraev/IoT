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
*/

/*
  Slightly modified Adalight protocol implementation that uses FastLED
  library (http://fastled.io) for driving WS2812B led stripe
*/

#include <Arduino.h>
#include <FastLED.h>

#define NUM_LEDS        255 // max leds
#define LED_PIN         5

#define SERIAL_SPEED    115200
#define SERIAL_TIMEOUT  10000
#define MAX_BRIGHTNESS  255

CRGB leds[NUM_LEDS];
uint8_t * ledsRaw = (uint8_t *)leds;

// A 'magic word' (along with LED count & checksum) precedes each block
// of LED data; this assists the microcontroller in syncing up with the
// host-side software and properly issuing the latch (host I/O is
// likely buffered, making usleep() unreliable for latch). You may see
// an initial glitchy frame or two until the two come into alignment.
// The magic word can be whatever sequence you like, but each character
// should be unique, and frequent pixel values like 0 and 255 are
// avoided -- fewer false positives. The host software will need to
// generate a compatible header: immediately following the magic word
// are three bytes: a 16-bit count of the number of LEDs (high byte
// first) followed by a simple checksum value (high byte XOR low byte
// XOR 0x55). LED data follows, 3 bytes per LED, in order R, G, B,
// where 0 = off and 255 = max brightness.
static const uint8_t magic[] = {'A', 'd', 'a'};
#define MAGICSIZE       sizeof(magic)
#define HEADERSIZE      (MAGICSIZE + 3)

#define MODE_HEADER     0
#define MODE_DATA       1

// Dirty trick: the circular buffer for serial data is 256 bytes,
// and the "in" and "out" indices are unsigned 8-bit types -- this
// much simplifies the cases where in/out need to "wrap around" the
// beginning/end of the buffer. Otherwise there'd be a ton of bit-
// masking and/or conditional code every time one of these indices
// needs to change, slowing things down tremendously.
uint8_t buffer[256];
uint8_t indexIn = 0, indexOut = 0, mode = MODE_HEADER;
uint8_t hi, lo, chk, i;
int16_t bytesBuffered = 0;
int16_t c;
int32_t outPos = 0;
int32_t bytesRemaining;
unsigned long startTime, lastByteTime, lastAckTime, currentTime;

void setup()
{
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setTemperature(TypicalLEDStrip);
  FastLED.setBrightness(MAX_BRIGHTNESS);
  Serial.begin(SERIAL_SPEED);
  Serial.print("Ada\n");
  startTime = micros();
  lastByteTime = lastAckTime = millis();

  for(;;)
  {
    // Implementation is a simple finite-state machine.
    // Regardless of mode, check for serial input each time:
    currentTime = millis();
    if ((bytesBuffered < 256) && ((c = Serial.read()) >= 0))
    {
      buffer[indexIn++] = c;
      bytesBuffered++;
      // Reset timeout counters
      lastByteTime = lastAckTime = currentTime;
    }
    else
    {
      // No data received. If this persists, send an ACK packet
      // to host once every second to alert it to our presence.
      if ((currentTime - lastAckTime) > 1000)
      {
        // Send ACK to host and Reset counter
        Serial.print("Ada\n");
        lastAckTime = currentTime;
      }
      // If no data received for an extended time, turn off all LEDs.
      if ((currentTime - lastByteTime) > SERIAL_TIMEOUT)
      {
        memset(leds, 0, NUM_LEDS * sizeof(struct CRGB));
        FastLED.show();
        lastByteTime = currentTime;
      }
    }

    switch (mode)
    {
      case MODE_HEADER:
        // In header-seeking mode. Is there enough data to check?
        if (bytesBuffered >= HEADERSIZE)
        {
          // Indeed. Check for a 'magic word' match.
          for (i = 0; (i < MAGICSIZE) && (buffer[indexOut++] == magic[i++]););
          if (i == MAGICSIZE)
          {
            // Magic word matches. Now how about the checksum?
            hi  = buffer[indexOut++];
            lo  = buffer[indexOut++];
            chk = buffer[indexOut++];
            if (chk == (hi ^ lo ^ 0x55))
            {
              // Checksum looks valid. Get 16-bit LED count, add 1
              // (# LEDs is always > 0) and multiply by 3 for R,G,B.
              bytesRemaining = 3L * (256L * (long)hi + (long)lo + 1L);
              bytesBuffered -= 3;
              outPos = 0;
              memset(leds, 0, NUM_LEDS * sizeof(struct CRGB));
              mode = MODE_DATA;
            }
            else
              // Checksum didn't match; search resumes after magic word. Rewind.
              indexOut -= 3;
          }
          // else no header match. Resume at first mismatched byte.
          bytesBuffered -= i;
        }
        break;
      case MODE_DATA:
        if (bytesRemaining > 0)
        {
          if (bytesBuffered > 0)
          {
            if (outPos < sizeof(leds))
              ledsRaw[outPos++] = buffer[indexOut++];
            bytesBuffered--;
            bytesRemaining--;
          }
        }
        // If serial buffer is threatening to underrun, start
        // introducing progressively longer pauses to allow more
        // data to arrive (up to a point).
        else
        {
          startTime = micros();
          mode = MODE_HEADER;
          FastLED.show();
        }
    }
  }
}

void loop()
{
}
