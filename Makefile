BUILD_DIR = $(shell pwd)/build
DIST_DIR = $(shell pwd)/dist
DEV_DATA_DIR = $(shell pwd)/data
DEV_LLVM_DIR = /usr/lib/llvm18

# Default target: build for development
.PHONY: build
build:
	touch $(BUILD_DIR)/build/.stamp
	$(MAKE) $(BUILD_DIR)/install/.stamp

$(BUILD_DIR)/build/.stamp:
	uv run meson.py setup $(BUILD_DIR)/build --prefix=/ -Dllvm_dir=$(DEV_LLVM_DIR)
	touch $@

$(BUILD_DIR)/install/.stamp: $(BUILD_DIR)/build/.stamp
	uv run meson.py install -C $(BUILD_DIR)/build --destdir=$(BUILD_DIR)/install
	touch $@

# Run a development PostgreSQL instance
.PHONY: run
run: $(DEV_DATA_DIR)/.stamp
	$(BUILD_DIR)/install/bin/postgres -D $(DEV_DATA_DIR)

$(DEV_DATA_DIR)/.stamp: $(BUILD_DIR)/install/.stamp
	$(BUILD_DIR)/install/bin/initdb --auth=trust -D $(DEV_DATA_DIR)
	touch $@

# Build standalone binary with all dependencies embedded statically for production use
.PHONY: standalone
standalone: $(DIST_DIR)/usr/bin/rustica-engine

$(DIST_DIR)/usr/bin/rustica-engine: $(BUILD_DIR)/llvm/.stamp
	uv run meson.py \
		setup $(BUILD_DIR)/build \
		--prefix=/usr \
		--buildtype=release \
		-Dembed_libs=true \
		-Dllvm_dir=$(BUILD_DIR)/llvm \
		-Dzlib_dir=$(BUILD_DIR)/zlib
	uv run meson.py \
		install -C $(BUILD_DIR)/build \
		--destdir=$(DIST_DIR) \
		--skip-subprojects \
		--tags standalone
	strip $@

$(BUILD_DIR)/build-zlib/.stamp:
	uv run meson.py \
		setup $(BUILD_DIR)/build-zlib \
		--prefix=$(BUILD_DIR)/zlib \
		--buildtype=release \
		-Dbuild_zlib_only=true
	touch $@

$(BUILD_DIR)/zlib/.stamp: $(BUILD_DIR)/build-zlib/.stamp
	uv run meson.py install -C $(BUILD_DIR)/build-zlib
	touch $@

$(BUILD_DIR)/build-llvm/.stamp: $(BUILD_DIR)/zlib/.stamp
	uv run meson.py \
		setup $(BUILD_DIR)/build-llvm \
		--prefix=$(BUILD_DIR)/llvm \
		--buildtype=release \
		-Dbuild_llvm_only=true \
		-Dzlib_dir=$(BUILD_DIR)/zlib
	touch $@

$(BUILD_DIR)/llvm/.stamp: $(BUILD_DIR)/build-llvm/.stamp
	uv run meson.py install -C $(BUILD_DIR)/build-llvm
	touch $@

# Build PostgreSQL extension with LLVM embedded statically for production use
.PHONY: extension
extension: $(DIST_DIR)/usr/lib/postgresql/rustica-engine.so

$(DIST_DIR)/usr/lib/postgresql/rustica-engine.so: $(BUILD_DIR)/llvm/.stamp
	uv run meson.py \
		setup $(BUILD_DIR)/build \
		--prefix=/usr \
		--buildtype=release \
		-Dllvm_dir=$(BUILD_DIR)/llvm \
		-Dzlib_dir=$(BUILD_DIR)/zlib
	uv run meson.py \
		install -C $(BUILD_DIR)/build \
		--destdir=$(DIST_DIR) \
		--skip-subprojects \
		--tags extension
	strip $@

# Build both standalone binary and PostgreSQL extension for OS distribution packaging
.PHONY: dist
dist:
	uv run meson.py setup $(BUILD_DIR)/build --prefix=/usr -Dllvm_dir=$(DEV_LLVM_DIR) --buildtype=release
	uv run meson.py install -C $(BUILD_DIR)/build --destdir=$(DIST_DIR) --skip-subprojects
	strip $(DIST_DIR)/usr/bin/rustica-engine $(DIST_DIR)/usr/lib/postgresql/rustica-engine.so

# Delete all build artifacts and installed files for development
.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)/build $(BUILD_DIR)/install

# Delete all build artifacts, development files, and distribution files
.PHONY: distclean
distclean: clean
	rm -rf $(BUILD_DIR) $(DIST_DIR) $(DEV_DATA_DIR)
