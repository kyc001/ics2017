NAME = klib

include $(AM_HOME)/Makefile.check
$(info Building $(NAME) [$(ARCH)])

LIB_DIR ?= $(shell pwd)
ARCHIVE ?= $(LIB_DIR)/build/$(NAME)-$(ARCH).a

.DEFAULT_GOAL = archive

.PHONY: archive clean

archive:
ifeq ($(ARCH), native)
	@:
else
	@test -f $(ARCHIVE) || { \
		echo "error: missing prebuilt archive $(ARCHIVE)"; \
		echo "error: klib sources are not present in this tree, so the archive cannot be rebuilt automatically."; \
		exit 1; \
	}
endif

clean:
	rm -rf $(LIB_DIR)/build/
