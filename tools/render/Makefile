# Host renderers for the DSP cores. Usage: make (builds and renders WAVs).

CXX ?= g++
CXXFLAGS ?= -std=c++11 -O2 -Wall -Wextra
OUT ?= ../../renders

all: render render_fx
	@mkdir -p $(OUT)
	./render $(OUT)
	./render_fx $(OUT)

render: render.cpp wav.hpp ../../oscillators/waveshaper/waveshaper_core.hpp ../../common/dsp_math.hpp
	$(CXX) $(CXXFLAGS) -o $@ render.cpp

render_fx: render_fx.cpp wav.hpp ../../common/freqshift_core.hpp ../../common/dungeon_core.hpp ../../common/dsp_math.hpp
	$(CXX) $(CXXFLAGS) -o $@ render_fx.cpp

clean:
	rm -f render render_fx

.PHONY: all clean
