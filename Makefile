RACK_DIR ?= ../..

SOURCES += $(wildcard src/*.cpp)
SOURCES += $(wildcard src/random8_core/*.cpp)
SOURCES += $(wildcard src/noise-plethora/*/*.cpp)

# OwlProgram sources (mirrors Befaco Oneiroi integration)
SOURCES += $(wildcard libs/OwlProgram/LibSource/*.cpp)
SOURCES += $(wildcard libs/OwlProgram/Libraries/KissFFT/*.c)
SOURCES += $(wildcard libs/OwlProgram/LibSource/*.c)

SOURCES := $(filter-out libs/OwlProgram/LibSource/ColourScreenPatch.cpp,$(SOURCES))
SOURCES := $(filter-out libs/OwlProgram/LibSource/MonochromeScreenPatch.cpp,$(SOURCES))
SOURCES := $(filter-out libs/OwlProgram/LibSource/PatchParameter.cpp,$(SOURCES))

FLAGS += -DVCV -Ilibs/OwlProgram/LibSource -Ilibs/OwlProgram/Source -Ilibs/OwlProgram/Libraries/KissFFT -Ilibs/OwlProgram/Libraries

DISTRIBUTABLES += $(wildcard LICENSE*) res

include $(RACK_DIR)/plugin.mk

# Iroi and Oneiroi ship their own (differing) Commons.h/Ui.h/Clock.h...
# keep the include dirs scoped per translation unit.
src/Iroi.o obj/src/Iroi.o: FLAGS += -Ilibs/Iroi
src/Oneiroi.o obj/src/Oneiroi.o: FLAGS += -Ilibs/Oneiroi

CXXFLAGS += -std=c++17

# debug only
# CXXFLAGS += -g -O0 
