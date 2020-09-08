#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

PROJECT_NAME := irrigation_ctrl

include $(IDF_PATH)/make/project.mk

MONITOR_OPTS += --eol CRLF

# # Create a SPIFFS image from the contents of the 'spiffs' directory
# # that fits the partition named 'cfg_store'. FLASH_IN_PROJECT indicates that
# # the generated image should be flashed when the entire project is flashed to
# # the target with 'make flash'. 
# #SPIFFS_IMAGE_FLASH_IN_PROJECT := 1
# $(eval $(call spiffs_create_partition_image,cfg_store,spiffs))

.PHONY: doc
doc:
	doxygen doc/Doxyfile

include Makefile.ota.incl
PKG_VERS = $(shell cat main/include/version.h | grep -oP '\#define PACKAGE_VERSION "\K.+?(?=")')
GIT_VERS = $(shell cat main/include/version.h | grep -oP '\#define GIT_SHA1 "\K.+?(?=")')
FULL_VERS = $(PKG_VERS)-$(GIT_VERS)
BIN_CHKSUM = $(shell md5sum  $(BUILD_DIR_BASE)/$(PROJECT_NAME).bin | grep -oP '\K\w+?(?= .*)')

OTA_METADATA_FILE_FS = $(OTA_DEST_DIR_BASE)/$(OTA_SERVER_DIR)/$(OTA_METADATA_FILE)
OTA_METADATA_FILE_SRV = /$(OTA_SERVER_DIR)/$(OTA_METADATA_FILE)

CFLAGS += -DOTA_METADATA_FILE=\"$(OTA_METADATA_FILE_SRV)\"
CXXFLAGS += -DOTA_METADATA_FILE=\"$(OTA_METADATA_FILE_SRV)\"

ota: app $(BUILD_DIR_BASE)/$(PROJECT_NAME).bin
	echo "New OTA version: $(FULL_VERS)"
	echo "MD5: $(BIN_CHKSUM)"
	cp -f  $(BUILD_DIR_BASE)/$(PROJECT_NAME).bin $(OTA_DEST_DIR_BASE)/$(OTA_SERVER_DIR)/$(PROJECT_NAME)-$(FULL_VERS).bin
	perl -i -pe 's/^(FILE=\/esp32\/).*$$/$${1}$(PROJECT_NAME)-$(FULL_VERS).bin/g' $(OTA_METADATA_FILE_FS)
	perl -i -pe 's/^(VERSION=).*$$/$${1}$(FULL_VERS)/g' $(OTA_METADATA_FILE_FS)
	perl -i -pe 's/^(MD5SUM=).*$$/$${1}$(BIN_CHKSUM)/g' $(OTA_METADATA_FILE_FS)
	bash -c $(OTA_POST_CMD)
