#
# "main" pseudo-component makefile.
#
# (Uses default behaviour of compiling all source files in directory, adding 'include' to include path.)

COMPONENT_ADD_INCLUDEDIRS := . include

GIT_VERSION := $(shell git describe --abbrev=8 --dirty --always --tags)
CFLAGS += -DGIT_VERSION=\"$(GIT_VERSION)\"
CXXFLAGS += -DGIT_VERSION=\"$(GIT_VERSION)\"

COMPONENT_EMBED_TXTFILES := ota_root_ca_cert.pem ota_host_public_key.pem irrigationConfig.default.json
# override the default build target to touch version.h
.PHONY: build
build: update_version $(COMPONENT_LIBRARY)

.PHONY: update_version
update_version:
	touch $(COMPONENT_PATH)/include/version.h
