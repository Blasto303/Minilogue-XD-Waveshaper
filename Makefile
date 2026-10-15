# Top-level shortcuts.
#   make         build every unit (.mnlgxdunit files next to each project)
#   make render  render the WAV demos into renders/
#   make clean

UNITS := oscillators/waveshaper effects/freqshift effects/spiral \
         effects/dungeon/growl effects/dungeon/metal effects/dungeon/void

all:
	@for u in $(UNITS); do $(MAKE) -C $$u && $(MAKE) -C $$u install || exit 1; done

render:
	$(MAKE) -C tools/render

clean:
	@for u in $(UNITS); do $(MAKE) -C $$u clean; done
	$(MAKE) -C tools/render clean

.PHONY: all render clean
