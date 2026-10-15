# Host renderer for the waveshaper core. Usage: make (builds and renders WAVs).

CXX ?= g++
CXXFLAGS ?= -std=c++11 -O2 -Wall -Wextra
OUT ?= ../../renders

all: render
	@mkdir -p $(OUT)
	./render $(OUT)

render: render.cpp ../../oscillators/waveshaper/waveshaper_core.hpp
	$(CXX) $(CXXFLAGS) -o $@ render.cpp

clean:
	rm -f render

.PHONY: all clean
