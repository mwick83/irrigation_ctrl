#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

PROJECT_NAME := irrigation_ctrl

CFLAGS += -DCONFIG_MONGOOSE_PRESENT -DMG_ENABLE_HTTP_STREAMING_MULTIPART -DMG_ENABLE_FILESYSTEM -DMG_DISABLE_DIRECTORY_LISTING=1 -DMG_DISABLE_DAV=1 -DMG_DISABLE_CGI=1
CXXFLAGS += -DCONFIG_MONGOOSE_PRESENT -DMG_ENABLE_HTTP_STREAMING_MULTIPART -DMG_ENABLE_FILESYSTEM -DMG_DISABLE_DIRECTORY_LISTING=1 -DMG_DISABLE_DAV=1 -DMG_DISABLE_CGI=1

include $(IDF_PATH)/make/project.mk

MONITOR_OPTS += --eol CRLF

.PHONY: doc
doc:
	doxygen doc/Doxyfile

include Makefile.paths.incl
GIT_VERS = $(shell git describe --abbrev=8 --dirty --always --tags)
PKG_VERS = $(shell cat main/include/version.h | grep -oP '\#define PACKAGE_VERSION "\K.+?(?=")')
FULL_VERS = $(PKG_VERS)-$(GIT_VERS)
BIN_CHKSUM = $(shell md5sum build/$(PROJECT_NAME).bin | grep -oP '\K\w+?(?= .*)')

OTA_METADATA_FILE_FS = $(OTA_DEST_DIR_BASE)/$(OTA_SERVER_DIR)/$(OTA_METADATA_FILE)
OTA_METADATA_FILE_SRV = /$(OTA_SERVER_DIR)/$(OTA_METADATA_FILE)

CFLAGS += -DOTA_METADATA_FILE=\"$(OTA_METADATA_FILE_SRV)\"
CXXFLAGS += -DOTA_METADATA_FILE=\"$(OTA_METADATA_FILE_SRV)\"

ota:
	echo "New OTA version: $(FULL_VERS)"
	echo "MD5: $(BIN_CHKSUM)"
	cp -f build/$(PROJECT_NAME).bin $(OTA_DEST_DIR_BASE)/$(OTA_SERVER_DIR)/$(PROJECT_NAME)-$(FULL_VERS).bin
	perl -i -pe 's/^(FILE=\/esp32\/).*$$/$${1}$(PROJECT_NAME)-$(FULL_VERS).bin/g' $(OTA_METADATA_FILE_FS)
	perl -i -pe 's/^(VERSION=).*$$/$${1}$(FULL_VERS)/g' $(OTA_METADATA_FILE_FS)
	perl -i -pe 's/^(MD5SUM=).*$$/$${1}$(BIN_CHKSUM)/g' $(OTA_METADATA_FILE_FS)
