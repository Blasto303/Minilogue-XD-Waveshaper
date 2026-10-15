# Top-level shortcuts.
#   make         build oscillators/waveshaper/waveshaper.mnlgxdunit
#   make render  render the WAV demos into renders/
#   make clean

all:
	$(MAKE) -C oscillators/waveshaper
	$(MAKE) -C oscillators/waveshaper install

render:
	$(MAKE) -C tools/render

clean:
	$(MAKE) -C oscillators/waveshaper clean
	$(MAKE) -C tools/render clean

.PHONY: all render clean
