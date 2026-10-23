/* SPDX-License-Identifier: MIT
 *
 * Do the rack's block form and its per-sample form produce the same audio?
 *
 * The block form is only taken when no cable points backward, on the
 * argument that a forward cable "lands in its target's accumulator before
 * that target runs" and so has no sample delay to preserve. That is an
 * argument, not evidence. Built twice -- once normally, once with
 * FM1_FX_FORCE_SLOW -- the two checksums have to match exactly.
 */
#include "../engine/fm1_drone.h"
#include <stdio.h>

#define SR 44100u
#define BLOCK 256u
using namespace fm1;

int main(void)
{
    Drone d;
    int16_t buf[BLOCK * 2u];
    unsigned long sum = 0;
    long nz = 0;

    d.Init();
    /* Voices into FX1, FX1 into FX2, FX2 out: a forward chain two deep. */
    d.SetGlobal(G_FX(0, G_FX_TYPE), FX_CHORUS);
    d.SetGlobal(G_FX(0, G_FX_DEST), DEST_FX2);
    d.SetGlobal(G_FX(0, G_FX_MIX),  90);
    d.SetGlobal(G_FX(1, G_FX_TYPE), FX_LP);
    d.SetGlobal(G_FX(1, G_FX_DEST), DEST_OUT);
    d.SetGlobal(G_FX(1, G_FX_MIX),  110);
    for (uint8_t v = 0; v < FM1_VOICES; v++) {
        d.SetParam(v, P_SHAPE,  braids::MACRO_OSC_SHAPE_TRIPLE_SAW);
        d.SetParam(v, P_LEVEL,  100);
        d.SetParam(v, P_PAN,    (uint8_t)(20 + v * 29));
        d.SetParam(v, P_DEST,   DEST_FX1);
        d.NoteOn(v, (uint8_t)(29 + v * 7));
    }
    d.SnapParams();

    for (uint32_t f = 0; f < SR * 2u; f += BLOCK) {
        for (uint32_t i = 0; i < BLOCK * 2u; i++) buf[i] = 0;
        d.Render(buf, BLOCK);
        for (uint32_t i = 0; i < BLOCK * 2u; i++) {
            sum = sum * 1000003u + (unsigned long)(uint16_t)buf[i];
            if (buf[i]) nz++;
        }
    }
    printf("checksum %lu   non-zero %ld\n", sum, nz);
    return 0;
}
