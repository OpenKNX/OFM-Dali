#include <stdlib.h>
// Host test for src/RgbwHelper.h (no Arduino/KNX needed):
//   g++ -std=c++11 -Wall -Isrc test/test_rgbw_helper.cpp -o /tmp/test_rgbw && /tmp/test_rgbw
#include <cstdio>
#include <cstring>
#include "RgbwHelper.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

int main()
{
    uint8_t color[4] = {1, 2, 3, 4};

    // all channels valid
    { uint8_t d[6] = {10, 20, 30, 40, 0, 0x0F};
      CHECK(RgbwHelper::decode(d, color) == 0x0F);
      CHECK(color[0] == 10 && color[1] == 20 && color[2] == 30 && color[3] == 40); }

    // only white valid -> RGB untouched
    { uint8_t d[6] = {99, 99, 99, 200, 0, 0x01};
      CHECK(RgbwHelper::decode(d, color) == 0x01);
      CHECK(color[0] == 10 && color[1] == 20 && color[2] == 30 && color[3] == 200); }

    // only red valid
    { uint8_t d[6] = {77, 99, 99, 99, 0, 0x08};
      CHECK(RgbwHelper::decode(d, color) == 0x08);
      CHECK(color[0] == 77 && color[1] == 20 && color[2] == 30 && color[3] == 200); }

    // only green + blue valid
    { uint8_t d[6] = {99, 5, 6, 99, 0, 0x06};
      CHECK(RgbwHelper::decode(d, color) == 0x06);
      CHECK(color[0] == 77 && color[1] == 5 && color[2] == 6 && color[3] == 200); }

    // nothing valid -> nothing changes
    { uint8_t before[4]; memcpy(before, color, 4);
      uint8_t d[6] = {1, 1, 1, 1, 0, 0x00};
      CHECK(RgbwHelper::decode(d, color) == 0);
      CHECK(memcmp(before, color, 4) == 0); }

    // reserved upper bits of byte 5 are ignored
    { uint8_t d[6] = {1, 2, 3, 4, 0, 0xF0};
      CHECK(RgbwHelper::decode(d, color) == 0); }

    // encode
    { uint8_t c[4] = {1, 2, 3, 4}; uint8_t d[6];
      RgbwHelper::encode(c, d);
      const uint8_t exp[6] = {1, 2, 3, 4, 0, 0x0F};
      CHECK(memcmp(d, exp, 6) == 0); }

    // isOff: only true when R, G, B and W are all 0
    { uint8_t z[4] = {0, 0, 0, 0}; CHECK(RgbwHelper::isOff(z)); }
    { uint8_t w[4] = {0, 0, 0, 1}; CHECK(!RgbwHelper::isOff(w)); }
    { uint8_t r[4] = {1, 0, 0, 0}; CHECK(!RgbwHelper::isOff(r)); }

    // all channels 0 in the telegram -> "off", but a partial 0 (only white) is not "off" while RGB is set
    { uint8_t c[4] = {10, 0, 0, 50}; uint8_t d[6] = {0, 0, 0, 0, 0, 0x01};
      RgbwHelper::decode(d, c);
      CHECK(!RgbwHelper::isOff(c)); }
    { uint8_t c[4] = {0, 0, 0, 50}; uint8_t d[6] = {0, 0, 0, 0, 0, 0x01};
      RgbwHelper::decode(d, c);
      CHECK(RgbwHelper::isOff(c)); }

    // DALI level: 255 is MASK
    CHECK(RgbwHelper::daliLevel(255) == 254);
    CHECK(RgbwHelper::daliLevel(254) == 254);
    CHECK(RgbwHelper::daliLevel(0) == 0);
    CHECK(RgbwHelper::knxFromDaliLevel(254) == 255);
    CHECK(RgbwHelper::knxFromDaliLevel(128) == 128);
    CHECK(RgbwHelper::knxFromDaliLevel(0) == 0);

    // colour from the driver report values (R, G, B, W; 255 = MASK = unknown)
    { const uint8_t rep[4] = {0, 0, 254, 0}; uint8_t c[4] = {9, 9, 9, 9};
      CHECK(RgbwHelper::colourFromDriver(rep, c));
      CHECK(c[0] == 0 && c[1] == 0 && c[2] == 255 && c[3] == 0); }
    { const uint8_t rep[4] = {254, 0, 0, 254}; uint8_t c[4];
      CHECK(RgbwHelper::colourFromDriver(rep, c));
      CHECK(c[0] == 255 && c[1] == 0 && c[2] == 0 && c[3] == 255); }
    { const uint8_t rep[4] = {0, 0, 0, 0}; uint8_t c[4] = {7, 7, 7, 7};
      CHECK(!RgbwHelper::colourFromDriver(rep, c));
      CHECK(c[0] == 7 && c[3] == 7); }                // unchanged
    { const uint8_t rep[4] = {255, 255, 255, 255}; uint8_t c[4] = {7, 7, 7, 7};
      CHECK(!RgbwHelper::colourFromDriver(rep, c)); }
    { const uint8_t rep[4] = {0, 0, 254, 255}; uint8_t c[4];
      CHECK(!RgbwHelper::colourFromDriver(rep, c)); } // one MASK is enough to reject

    // slider mode: brightness = largest channel, colour = ratio
    { const uint8_t c[4] = {128, 0, 0, 128}; CHECK(RgbwHelper::maxChannel(c) == 128); }
    { const uint8_t c[4] = {0, 0, 0, 0};     CHECK(RgbwHelper::maxChannel(c) == 0); }
    { const uint8_t c[4] = {5, 0, 0, 5}; uint8_t m[4];
      RgbwHelper::normalise(c, m);
      CHECK(m[0] == 255 && m[1] == 0 && m[2] == 0 && m[3] == 255); }
    { const uint8_t c[4] = {241, 0, 13, 241}; uint8_t m[4];
      RgbwHelper::normalise(c, m);
      CHECK(m[0] == 255 && m[1] == 0 && m[2] == 14 && m[3] == 255); }
    { const uint8_t c[4] = {0, 0, 0, 0}; uint8_t m[4] = {9, 9, 9, 9};
      RgbwHelper::normalise(c, m);
      CHECK(RgbwHelper::isOff(m)); }
    { const uint8_t m[4] = {255, 0, 0, 255}; uint8_t c[4];
      RgbwHelper::scale(m, 128, c);
      CHECK(c[0] == 128 && c[1] == 0 && c[2] == 0 && c[3] == 128);
      RgbwHelper::scale(m, 255, c);
      CHECK(c[0] == 255 && c[3] == 255);
      RgbwHelper::scale(m, 5, c);
      CHECK(c[0] == 5 && c[3] == 5); }
    { const uint8_t m[4] = {255, 102, 0, 51}; uint8_t c[4];
      RgbwHelper::scale(m, 5, c);
      CHECK(c[0] == 5 && c[1] == 2 && c[2] == 0 && c[3] == 1); }
    // round trip colour -> mix -> same amplitude gives the colour back (within 1)
    for (int a = 1; a < 256; a++)
    {
        const uint8_t c[4] = {(uint8_t)a, (uint8_t)(a / 3), 0, (uint8_t)(a / 2)};
        uint8_t m[4], b[4];
        RgbwHelper::normalise(c, m);
        RgbwHelper::scale(m, RgbwHelper::maxChannel(c), b);
        for (int i = 0; i < 4; i++)
            CHECK(abs((int)b[i] - (int)c[i]) <= 1);
        CHECK(RgbwHelper::maxChannel(m) == 255);
    }
    CHECK(RgbwHelper::amplitudeFromPercent(0) == 0);
    CHECK(RgbwHelper::amplitudeFromPercent(100) == 255);
    CHECK(RgbwHelper::amplitudeFromPercent(50) == 128);
    CHECK(RgbwHelper::amplitudeFromPercent(0.1f) == 0 || RgbwHelper::amplitudeFromPercent(0.1f) == 1);
    CHECK(RgbwHelper::percentFromAmplitude(255) > 99.99f && RgbwHelper::percentFromAmplitude(255) < 100.01f);
    CHECK(RgbwHelper::percentFromAmplitude(0) == 0.0f);
    for (int a = 0; a < 256; a++)
        CHECK(RgbwHelper::amplitudeFromPercent(RgbwHelper::percentFromAmplitude(a)) == a);

    // group: decode on an all-0 base gives a complete colour, channels that are not valid stay 0
    { uint8_t d[6] = {99, 99, 200, 99, 0, 0x02}; uint8_t c[4] = {0, 0, 0, 0};
      CHECK(RgbwHelper::decode(d, c) == 0x02);
      CHECK(c[0] == 0 && c[1] == 0 && c[2] == 200 && c[3] == 0); }

    printf(failures ? "%d FAILED\n" : "all tests ok\n", failures);
    return failures ? 1 : 0;
}
