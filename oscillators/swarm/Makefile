# Builds the oscillator with the minilogue xd template Makefile from the
# logue SDK submodule. Usage: make, make install, make clean.

PLATFORMDIR := $(abspath ../../logue-sdk/platform/minilogue-xd)

# Use the system arm-none-eabi-gcc if Korg's bundled toolchain is missing.
ifeq ($(wildcard $(PLATFORMDIR)/../../tools/gcc/gcc-arm-none-eabi-5_4-2016q3/bin),)
GCC_BIN_PATH ?= $(patsubst %/,%,$(dir $(shell which arm-none-eabi-gcc)))
endif

ZIP ?= $(shell which zip)

include $(PLATFORMDIR)/dummy-osc/Makefile
