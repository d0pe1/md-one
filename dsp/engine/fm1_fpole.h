/* SPDX-License-Identifier: MIT
 *
 * The filter's slope, split out so C can see it.
 *
 * Same reason fm1_fmode.h exists: fm1_filter.h embeds braids::Svf and is
 * therefore C++ only, while the firmware's UI is C and has to put "12DB" on
 * a screen. These are parameter VALUES and belong beside the P_* indices.
 *
 *   6 dB   one pole, from the same coefficient table the SVF reads. The
 *          cheapest block in the voice: one multiply and one add.
 *  12 dB   the state-variable filter on its own.
 *  24 dB   the same filter twice -- exactly double the cost, and only when
 *          asked for.
 *
 * BP and NOTCH ignore it and always run at 12 dB. A one-pole band pass is
 * not a thing, and cascading a notch deepens it rather than steepening it.
 */
#ifndef FM1_FPOLE_H
#define FM1_FPOLE_H

enum { FP_6 = 0, FP_12, FP_24, FP_NPOLES };

static const char *const FM1_FPOLE_NAME[FP_NPOLES] = { "6DB", "12DB", "24DB" };

#endif /* FM1_FPOLE_H */
