VENDOR_DIR = vendor
WAMR_VERSION = 2.1.0
WAMR_DIR = $(VENDOR_DIR)/wamr-$(WAMR_VERSION)

MODULE_big = rustica-wamr
OBJS = src/master.o

EXTENSION = rustica-wamr
DATA = sql/rustica-wamr--1.0.sql

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)

include $(PGXS)

$(VENDOR_DIR):
	mkdir -p $(VENDOR_DIR)

WAMR_TARBALL = "$(VENDOR_DIR)/wamr-$(WAMR_VERSION).tar.gz"
$(WAMR_TARBALL): $(VENDOR_DIR)
	wget -O $(WAMR_TARBALL) \
		"https://github.com/bytecodealliance/wasm-micro-runtime/archive/refs/tags/WAMR-$(WAMR_VERSION).tar.gz"

$(WAMR_DIR): $(WAMR_TARBALL)
	mkdir -p $(WAMR_DIR)
	tar xvf $(WAMR_TARBALL) -C $(WAMR_DIR) --strip-components=1

.PHONY: clean-vendor
clean-vendor:
	rm -rf $(VENDOR_DIR)

all: $(WAMR_DIR)
