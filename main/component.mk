#
# "main" pseudo-component makefile.
#
# (Uses default behaviour of compiling all source files in directory, adding 'include' to include path.)

COMPONENT_ADD_INCLUDEDIRS := . include

GIT_VERSION := $(shell git describe --abbrev=8 --dirty --always --tags)
CFLAGS += -DGIT_VERSION=\"$(GIT_VERSION)\"
CXXFLAGS += -DGIT_VERSION=\"$(GIT_VERSION)\"
