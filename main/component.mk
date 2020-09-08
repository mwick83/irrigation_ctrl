#
# "main" pseudo-component makefile.
#
# (Uses default behaviour of compiling all source files in directory, adding 'include' to include path.)

COMPONENT_ADD_INCLUDEDIRS := . include

COMPONENT_EMBED_TXTFILES := ota_root_ca_cert.pem ota_host_public_key.pem irrigationConfig.default.json

# override the default build target to update version.h if needed
build: $(COMPONENT_PATH)/include/version.h $(COMPONENT_LIBRARY)

GIT_SHA1 := $(shell git describe --match=NeVeRmAtCh --always --abbrev=40 --dirty --tags)

.PHONY: $(COMPONENT_PATH)/include/version.h
$(COMPONENT_PATH)/include/version.h: $(COMPONENT_PATH)/include/version.h.in
	sed 's/@GIT_SHA1@/$(GIT_SHA1)/' < $< > $@.tmp
	if cmp -s $@.tmp $@ ; then : ; else \
		mv $@.tmp $@ ; \
	fi
	rm -f $@.tmp
