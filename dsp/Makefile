# SPDX-License-Identifier: MIT
#
# Host-side DSP harness for the FM-1 laboratory.
#
#   make            build render, bench and the tests
#   make test       run the deterministic tests
#   make bench      run the budget benchmark
#   make demo       render a sine and an FM tone into build/
#   make target     compile dsp/shared/ for pi32v2 and check the target rules
#   make clean
#
# dsp/shared/ is the code that also goes on the device: integer only.
# dsp/host/   is host-only and may use float and libm (the reference).

CC      ?= cc
CXX     ?= c++
CFLAGS  ?= -O2 -std=c99 -Wall -Wextra -Wconversion -Wshadow
LDLIBS  ?= -lm
BUILD   := build

SHARED  := shared/fm1_osc.c
HOSTLIB := host/wav.c host/ref_osc.c

# --- vendored Mutable Instruments Braids (MIT) ------------------------------
# Warnings are NOT tightened here: this is upstream code we keep byte-identical
# so it can be re-pulled. Our own adapter is dsp/mi/fm1_braids.cc.
MI_DIR   := mi
MI_SRC   := $(MI_DIR)/fm1_braids.cc \
            $(MI_DIR)/braids/analog_oscillator.cc \
            $(MI_DIR)/braids/digital_oscillator.cc \
            $(MI_DIR)/braids/macro_oscillator.cc \
            $(MI_DIR)/braids/resources.cc \
            $(MI_DIR)/stmlib/utils/random.cc
MI_INC   := -I$(MI_DIR) -I$(MI_DIR)/stmlib/..
MI_FLAGS := -O2 -Wall -fno-exceptions -fno-rtti

# --- our drone engine (MIT, written here) -----------------------------------
ENG_DIR := engine
ENG_SRC := $(ENG_DIR)/fm1_drone.cc
ENG_HDR := $(wildcard $(ENG_DIR)/*.h) $(wildcard shared/*.h)

.PHONY: all test bench demo target clean table braids braids-all braids-bench braids-ram drone drone-all gain fresp envshape
all: $(BUILD)/render $(BUILD)/bench $(BUILD)/test_osc $(BUILD)/render_braids

$(BUILD):
	mkdir -p $(BUILD)

# The generated sine table is a build input; regenerate it if the generator moved.
shared/fm1_sine_table.h: ../scripts/gen_sine_table.py
	python3 ../scripts/gen_sine_table.py

table: shared/fm1_sine_table.h

$(BUILD)/render: host/render.c $(SHARED) host/wav.c shared/fm1_sine_table.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ host/render.c $(SHARED) host/wav.c

$(BUILD)/bench: host/bench.c $(SHARED) shared/fm1_sine_table.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ host/bench.c $(SHARED)

$(BUILD)/test_osc: tests/test_osc.c $(SHARED) host/ref_osc.c shared/fm1_sine_table.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_osc.c $(SHARED) host/ref_osc.c $(LDLIBS)

$(BUILD)/test_comb: tests/test_comb.c $(ENG_HDR) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_comb.c $(LDLIBS)

# Unlike the two C tests, this one links the whole engine: the bus and the
# limiter exist only inside Drone::Render, and having no test that went
# through it is what let the per-voice saturation defect survive this long.
$(BUILD)/test_mix: tests/test_mix.cc $(ENG_SRC) $(ENG_HDR) $(MI_SRC) $(MI_HDR) | $(BUILD)
	$(CXX) $(MI_FLAGS) $(MI_INC) -o $@ tests/test_mix.cc $(ENG_SRC) $(MI_SRC)

# Pure data and pure logic: no engine, no Braids, no libm. The expensive
# assertion here is reachability -- that every parameter in fm1_params.h can
# be found by a human turning a knob.
$(BUILD)/test_pages: tests/test_pages.c $(ENG_HDR) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_pages.c

test: $(BUILD)/test_osc $(BUILD)/test_comb $(BUILD)/test_mix $(BUILD)/test_pages
	./$(BUILD)/test_osc
	@echo
	./$(BUILD)/test_comb
	@echo
	./$(BUILD)/test_mix
	@echo
	./$(BUILD)/test_pages

bench: $(BUILD)/bench
	./$(BUILD)/bench

demo: $(BUILD)/render
	./$(BUILD)/render --hz 220 --sec 2 --out $(BUILD)/sine-220.wav
	./$(BUILD)/render --hz 220 --pm 110 --index 12000 --sec 2 --out $(BUILD)/fm-220x110.wav

# --- Braids ----------------------------------------------------------------
# Headers are listed explicitly: the pitch correction lives in fm1_braids.h, so
# a rule that depends only on .cc files silently keeps a stale binary after an
# edit there -- which cost a debugging cycle once already.
MI_HDR := $(MI_DIR)/fm1_braids.h $(wildcard $(MI_DIR)/braids/*.h) \
          $(wildcard $(MI_DIR)/stmlib/*.h) $(wildcard $(MI_DIR)/stmlib/utils/*.h)

$(BUILD)/render_braids: host/render_braids.cc host/wav.c host/wav.h $(MI_SRC) $(MI_HDR) | $(BUILD)
	$(CXX) $(MI_FLAGS) $(MI_INC) -o $@ host/render_braids.cc $(MI_SRC) host/wav.c

# One drone note, for listening and for checking the pitch correction.
$(BUILD)/bench_braids: host/bench_braids.cc $(MI_SRC) $(MI_HDR) | $(BUILD)
	$(CXX) $(MI_FLAGS) $(MI_INC) -o $@ host/bench_braids.cc $(MI_SRC)

braids-bench: $(BUILD)/bench_braids
	./$(BUILD)/bench_braids

$(BUILD)/footprint: host/footprint.cc $(MI_SRC) $(MI_HDR) | $(BUILD)
	$(CXX) $(MI_FLAGS) $(MI_INC) -o $@ host/footprint.cc $(MI_SRC)

braids-ram: $(BUILD)/footprint
	./$(BUILD)/footprint

# --- the drone engine -------------------------------------------------------
$(BUILD)/render_drone: host/render_drone.cc host/wav.c host/wav.h \
                       $(ENG_SRC) $(ENG_HDR) $(MI_SRC) $(MI_HDR) | $(BUILD)
	$(CXX) $(MI_FLAGS) $(MI_INC) -o $@ host/render_drone.cc $(ENG_SRC) $(MI_SRC) host/wav.c

drone: $(BUILD)/render_drone
	./$(BUILD)/render_drone --preset drift --sec 20 --out $(BUILD)/drone-drift.wav

# Where the level goes: peak dBFS per gain configuration, engine only. Answers
# "is the instrument quiet, or is the signal chain after it quiet?", which a
# DAW meter at the far end of a jack and a mixer preamp cannot.
$(BUILD)/probe_gain: host/probe_gain.cc $(ENG_SRC) $(ENG_HDR) $(MI_SRC) $(MI_HDR) | $(BUILD)
	$(CXX) $(MI_FLAGS) $(MI_INC) -o $@ host/probe_gain.cc $(ENG_SRC) $(MI_SRC) -lm

gain: $(BUILD)/probe_gain
	./$(BUILD)/probe_gain

drone-all: $(BUILD)/render_drone
	@for p in $$(./$(BUILD)/render_drone --list | awk '{print $$1}'); do \
	    ./$(BUILD)/render_drone --preset $$p --sec 20 --restrike 4000 \
	        --out $(BUILD)/drone-$$p.wav; \
	done

braids: $(BUILD)/render_braids
	./$(BUILD)/render_braids --shape triple_saw --note 36 --timbre 20000 --sec 4 \
	    --out $(BUILD)/braids-triple_saw-36.wav

# Audition every candidate drone shape at the same note.
braids-all: $(BUILD)/render_braids
	@for s in $$(./$(BUILD)/render_braids --list); do \
	    ./$(BUILD)/render_braids --shape $$s --note 36 --timbre 20000 --sec 3 \
	        --out $(BUILD)/braids-$$s.wav; \
	done

target:
	./target/check.sh

clean:
	rm -rf $(BUILD)

# What the voice filter actually does, against what the knob says: passband
# gain and the real -3 dB corner per CUT setting, per slope. This is what
# showed the Chamberlin SVF dying at note 115 with the panel reading 12.5 kHz.
$(BUILD)/filter_resp: host/filter_resp.cc $(ENG_SRC) $(ENG_HDR) $(MI_SRC) $(MI_HDR) | $(BUILD)
	$(CXX) $(MI_FLAGS) $(MI_INC) -o $@ host/filter_resp.cc $(ENG_SRC) $(MI_SRC) -lm

fresp: $(BUILD)/filter_resp
	./$(BUILD)/filter_resp

# ANALOG against DIGITAL: the fall itself, sampled every tenth of the way
# down, so "same time, different shape" is checked rather than claimed.
$(BUILD)/env_shape: host/env_shape.cc $(ENG_HDR) | $(BUILD)
	$(CXX) $(MI_FLAGS) -o $@ host/env_shape.cc

envshape: $(BUILD)/env_shape
	./$(BUILD)/env_shape
