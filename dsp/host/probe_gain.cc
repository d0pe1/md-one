/* SPDX-License-Identifier: MIT
 *
 * Where the level goes.
 *
 * The instrument measured -29 dBFS in a DAW with one voice sounding and
 * every level control at maximum. That reading is the whole path end to end
 * -- engine, DAC, jack, mixer preamp, converter -- so it cannot say which
 * stage lost it. This asks the engine alone, with nothing else in the way.
 *
 * Each case renders a second and reports the peak of what the engine hands
 * the platform, in dBFS of Q15. platform.c then shifts that left by 7 on its
 * way to the DAC, which is half of 24-bit full scale, so the device's own
 * output is 6 dB below every number printed here. That shift is inherited
 * from Dinghy and is not ours to change casually.
 */
#include "../engine/fm1_drone.h"

#include <stdio.h>
#include <math.h>

#define SR    44100u
#define BLOCK 256u

using namespace fm1;

static double db(int32_t peak)
{
    return peak <= 0 ? -999.0 : 20.0 * log10((double)peak / 32767.0);
}

/* One second, peak of both channels. Render ADDS, so the block is cleared
 * each time round -- exactly what the audio ISR does. */
static int32_t run(Drone &d)
{
    int16_t buf[BLOCK * 2u];
    int32_t peak = 0;

    for (uint32_t f = 0; f < SR; f += BLOCK) {
        for (uint32_t i = 0; i < BLOCK * 2u; i++) buf[i] = 0;
        d.Render(buf, BLOCK);
        for (uint32_t i = 0; i < BLOCK * 2u; i++) {
            int32_t a = buf[i] < 0 ? -(int32_t)buf[i] : (int32_t)buf[i];
            if (a > peak) peak = a;
        }
    }
    return peak;
}

/* A voice with every gain control wide open and nothing in its way: no comb,
 * no drive, filters passing. The loudest this engine can legitimately be
 * asked to be from one oscillator. */
static void bare(Drone &d, uint8_t v, uint8_t shape, uint8_t pan)
{
    d.SetParam(v, P_SHAPE,  shape);
    d.SetParam(v, P_TIMBRE, 64);
    d.SetParam(v, P_COLOR,  64);
    d.SetParam(v, P_LEVEL,  127);
    d.SetParam(v, P_PAN,    pan);
    d.NoteOn(v, 45);
}

static void one(const char *what, uint8_t shape, uint8_t pan, int voices)
{
    Drone d;
    d.Init();
    for (int v = 0; v < voices; v++) bare(d, (uint8_t)v, shape, pan);
    for (int v = voices; v < FM1_VOICES; v++) d.SetGate((uint8_t)v, 0);
    d.SnapParams();

    int32_t p = run(d);
    printf("  %-32s peak %6d   %7.1f dBFS   (%6.1f at the DAC)\n",
           what, p, db(p), db(p) - 6.0);
}

int main(void)
{
    printf("engine output, Q15 full scale = 32767\n");
    printf("platform.c shifts left by 7, so the DAC sees 6 dB less\n\n");

    printf("one voice, LEVEL 127:\n");
    one("CSAW, centre pan",     braids::MACRO_OSC_SHAPE_CSAW,         64, 1);
    one("CSAW, hard left",      braids::MACRO_OSC_SHAPE_CSAW,          0, 1);
    one("TRIPLE_SAW, centre",   braids::MACRO_OSC_SHAPE_TRIPLE_SAW,   64, 1);
    one("TRIPLE_SINE, centre",  braids::MACRO_OSC_SHAPE_TRIPLE_SINE,  64, 1);

    printf("\nfour voices, LEVEL 127, all centre:\n");
    one("CSAW x4",              braids::MACRO_OSC_SHAPE_CSAW,         64, 4);

    return 0;
}
