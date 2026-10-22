# Host renderers for the DSP cores. Usage: make (builds and renders WAVs).

CXX ?= g++
CXXFLAGS ?= -std=c++11 -O2 -Wall -Wextra
OUT ?= ../../renders

all: render render_fx render_gen
	@mkdir -p $(OUT)
	./render $(OUT)
	./render_fx $(OUT)
	./render_gen $(OUT)

render: render.cpp wav.hpp ../../oscillators/waveshaper/waveshaper_core.hpp ../../common/dsp_math.hpp
	$(CXX) $(CXXFLAGS) -o $@ render.cpp

render_fx: render_fx.cpp wav.hpp ../../common/freqshift_core.hpp ../../common/dungeon_core.hpp ../../common/grain_core.hpp ../../common/dsp_math.hpp
	$(CXX) $(CXXFLAGS) -o $@ render_fx.cpp

render_gen: render_gen.cpp wav.hpp ../../oscillators/swarm/swarm_core.hpp ../../common/dungeon_core.hpp ../../common/dsp_math.hpp
	$(CXX) $(CXXFLAGS) -o $@ render_gen.cpp

clean:
	rm -f render render_fx render_gen

.PHONY: all clean
