BUILD_DIR = $(shell pwd)/build
DIST_DIR = $(shell pwd)/dist
DEV_INSTALL_DIR = $(shell pwd)/install
DEV_DATA_DIR = $(shell pwd)/data
DEV_LLVM_DIR = /usr/lib/llvm18/lib/cmake/llvm

# Default target: build for development
.PHONY: build
build: $(DEV_INSTALL_DIR)

$(BUILD_DIR)/build.ninja:
	uv run meson.py setup $(BUILD_DIR) --prefix=/ -Dllvm_dir=$(DEV_LLVM_DIR)

$(DEV_INSTALL_DIR): $(BUILD_DIR)/build.ninja
	uv run meson.py install -C $(BUILD_DIR) --destdir=$(DEV_INSTALL_DIR)

# Run a development PostgreSQL instance
.PHONY: run
run: $(DEV_DATA_DIR)
	$(DEV_INSTALL_DIR)/bin/postgres -D $(DEV_DATA_DIR)

$(DEV_DATA_DIR): $(DEV_INSTALL_DIR)
	$(DEV_INSTALL_DIR)/bin/initdb --auth=trust -D $(DEV_DATA_DIR)

# Build standalone binary with all dependencies embedded statically for production use
.PHONY: standalone
standalone: $(DIST_DIR)/usr/bin/rustica-engine

$(DIST_DIR)/usr/bin/rustica-engine:
	uv run meson.py setup $(BUILD_DIR) --prefix=/usr -Dembed_libs=true --buildtype=release

# Build PostgreSQL extension with LLVM embedded statically for production use
.PHONY: extension
extension: $(DIST_DIR)/usr/lib/postgresql/rustica-engine.so

$(DIST_DIR)/usr/lib/postgresql/rustica-engine.so:
	uv run meson.py setup $(BUILD_DIR) --prefix=/usr --buildtype=release
	uv run meson.py install -C $(BUILD_DIR) --destdir=$(DIST_DIR) --skip-subprojects --tags extension
	strip $(DIST_DIR)/usr/lib/postgresql/rustica-engine.so

# Build both standalone binary and PostgreSQL extension for OS distribution packaging
.PHONY: dist
dist:
	uv run meson.py setup $(BUILD_DIR) --prefix=/usr -Dllvm_dir=$(DEV_LLVM_DIR) --buildtype=release
	uv run meson.py install -C $(BUILD_DIR) --destdir=$(DIST_DIR) --skip-subprojects
	strip $(DIST_DIR)/usr/bin/rustica-engine $(DIST_DIR)/usr/lib/postgresql/rustica-engine.so

# Delete all build artifacts and installed files for development
.PHONY: clean
clean:
	rm -rf $(BUILD_DIR) $(shell dirname $(BUILD_DIR))/build-staging $(DEV_INSTALL_DIR)

# Delete all build artifacts, development files, and distribution files
.PHONY: distclean
distclean: clean
	rm -rf $(DIST_DIR) $(DEV_DATA_DIR)
