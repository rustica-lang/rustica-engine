# Shared global config
DEV = 0
DEBUG = 0
GC = 1
BUNDLE_LLVM = 0
VENDOR_DIR = vendor
WAMR_VERSION = 2.1.2
LLHTTP_VERSION = 9.2.1
MODULE_big = rustica-wamr
SQL_BACKDOOR = 0

# Vendor paths
WAMR_DIR = $(VENDOR_DIR)/wamr-$(WAMR_VERSION)
WAMR_CORE_ROOT = $(WAMR_DIR)/core
WAMR_IWASM_ROOT = $(WAMR_CORE_ROOT)/iwasm
WAMR_SHARED_ROOT = $(WAMR_CORE_ROOT)/shared
WAMR_TARBALL = $(VENDOR_DIR)/wamr-$(WAMR_VERSION).tar.gz
LLHTTP_TARBALL = $(VENDOR_DIR)/llhttp-$(LLHTTP_VERSION).tar.gz
LLHTTP_SRC = $(VENDOR_DIR)/llhttp-$(LLHTTP_VERSION)

# PostgreSQL for development
ifeq ($(DEV),1)
DEV_PG_VERSION = 16.3
DEV_PG_TARBALL = $(VENDOR_DIR)/pg-$(DEV_PG_VERSION).tar.gz
DEV_PG_SRC = $(VENDOR_DIR)/pg-$(DEV_PG_VERSION)
DEV_PG_INSTALL = $(VENDOR_DIR)/pg-$(DEV_PG_VERSION)-install
DEV_PG_DATA = $(VENDOR_DIR)/pg-$(DEV_PG_VERSION)-data
DEV_PG_LOG = $(VENDOR_DIR)/pg-$(DEV_PG_VERSION).log
endif

ifeq ($(BUNDLE_LLVM),1)
LLVM_BUILD_DIR = $(WAMR_CORE_ROOT)/deps/llvm/build
endif

# PGXS config begin
OBJS = \
	$(WAMR_SHARED_ROOT)/platform/linux/platform_init.o \
	$(WAMR_SHARED_ROOT)/platform/common/posix/posix_thread.o \
	$(WAMR_SHARED_ROOT)/platform/common/posix/posix_time.o \
	$(WAMR_SHARED_ROOT)/platform/common/posix/posix_malloc.o \
	$(WAMR_SHARED_ROOT)/platform/common/posix/posix_memmap.o \
	$(WAMR_SHARED_ROOT)/mem-alloc/mem_alloc.o \
	$(WAMR_SHARED_ROOT)/mem-alloc/ems/ems_kfc.o \
	$(WAMR_SHARED_ROOT)/mem-alloc/ems/ems_alloc.o \
	$(WAMR_SHARED_ROOT)/mem-alloc/ems/ems_hmu.o \
	$(WAMR_SHARED_ROOT)/utils/bh_assert.o \
	$(WAMR_SHARED_ROOT)/utils/bh_bitmap.o \
	$(WAMR_SHARED_ROOT)/utils/bh_common.o \
	$(WAMR_SHARED_ROOT)/utils/bh_hashmap.o \
	$(WAMR_SHARED_ROOT)/utils/bh_leb128.o \
	$(WAMR_SHARED_ROOT)/utils/bh_list.o \
	$(WAMR_SHARED_ROOT)/utils/bh_log.o \
	$(WAMR_SHARED_ROOT)/utils/bh_queue.o \
	$(WAMR_SHARED_ROOT)/utils/bh_vector.o \
	$(WAMR_SHARED_ROOT)/utils/runtime_timer.o \
	$(WAMR_IWASM_ROOT)/common/wasm_application.o \
	$(WAMR_IWASM_ROOT)/common/wasm_runtime_common.o \
	$(WAMR_IWASM_ROOT)/common/wasm_native.o \
	$(WAMR_IWASM_ROOT)/common/wasm_exec_env.o \
	$(WAMR_IWASM_ROOT)/common/wasm_loader_common.o \
	$(WAMR_IWASM_ROOT)/common/wasm_memory.o \
	$(WAMR_IWASM_ROOT)/common/wasm_c_api.o \
	$(WAMR_IWASM_ROOT)/aot/aot_loader.o \
	$(WAMR_IWASM_ROOT)/aot/arch/aot_reloc_x86_64.o \
	$(WAMR_IWASM_ROOT)/aot/aot_runtime.o \
	$(WAMR_IWASM_ROOT)/aot/aot_intrinsic.o \
	$(WAMR_IWASM_ROOT)/compilation/aot.o \
	$(WAMR_IWASM_ROOT)/compilation/aot_compiler.o \
    $(WAMR_IWASM_ROOT)/compilation/aot_emit_aot_file.o \
    $(WAMR_IWASM_ROOT)/compilation/aot_emit_compare.o \
    $(WAMR_IWASM_ROOT)/compilation/aot_emit_const.o \
    $(WAMR_IWASM_ROOT)/compilation/aot_emit_control.o \
    $(WAMR_IWASM_ROOT)/compilation/aot_emit_conversion.o \
    $(WAMR_IWASM_ROOT)/compilation/aot_emit_exception.o \
    $(WAMR_IWASM_ROOT)/compilation/aot_emit_function.o \
    $(WAMR_IWASM_ROOT)/compilation/aot_emit_memory.o \
    $(WAMR_IWASM_ROOT)/compilation/aot_emit_numberic.o \
    $(WAMR_IWASM_ROOT)/compilation/aot_emit_parametric.o \
    $(WAMR_IWASM_ROOT)/compilation/aot_emit_stringref.o \
    $(WAMR_IWASM_ROOT)/compilation/aot_emit_table.o \
    $(WAMR_IWASM_ROOT)/compilation/aot_emit_variable.o \
    $(WAMR_IWASM_ROOT)/compilation/aot_llvm.o \
    $(WAMR_IWASM_ROOT)/compilation/aot_llvm_extra2.o \
    $(WAMR_IWASM_ROOT)/compilation/aot_llvm_extra.o \
    $(WAMR_IWASM_ROOT)/compilation/aot_orc_extra2.o \
    $(WAMR_IWASM_ROOT)/compilation/aot_orc_extra.o \
    $(WAMR_IWASM_ROOT)/compilation/simd/simd_access_lanes.o \
    $(WAMR_IWASM_ROOT)/compilation/simd/simd_bitmask_extracts.o \
    $(WAMR_IWASM_ROOT)/compilation/simd/simd_bit_shifts.o \
    $(WAMR_IWASM_ROOT)/compilation/simd/simd_bitwise_ops.o \
    $(WAMR_IWASM_ROOT)/compilation/simd/simd_bool_reductions.o \
    $(WAMR_IWASM_ROOT)/compilation/simd/simd_common.o \
    $(WAMR_IWASM_ROOT)/compilation/simd/simd_comparisons.o \
    $(WAMR_IWASM_ROOT)/compilation/simd/simd_construct_values.o \
    $(WAMR_IWASM_ROOT)/compilation/simd/simd_conversions.o \
    $(WAMR_IWASM_ROOT)/compilation/simd/simd_floating_point.o \
    $(WAMR_IWASM_ROOT)/compilation/simd/simd_int_arith.o \
    $(WAMR_IWASM_ROOT)/compilation/simd/simd_load_store.o \
    $(WAMR_IWASM_ROOT)/compilation/simd/simd_sat_int_arith.o \
    $(WAMR_IWASM_ROOT)/interpreter/wasm_runtime.o \
    $(WAMR_IWASM_ROOT)/interpreter/wasm_loader.o \
    $(WAMR_IWASM_ROOT)/interpreter/wasm_interp_classic.o \
    $(LLHTTP_SRC)/src/api.o \
    $(LLHTTP_SRC)/src/http.o \
    $(LLHTTP_SRC)/src/llhttp.o \
    $(patsubst %.c,%.o,$(shell find src -type f -name "*.c"))

WAMR_DEFINES = \
	-DBUILD_TARGET_X86_64 \
	-DBH_MALLOC=wasm_runtime_malloc \
	-DBH_FREE=wasm_runtime_free \
	-DWASM_ENABLE_LOG=1 \
	-DWASM_ENABLE_AOT=1 \
	-DWASM_ENABLE_AOT_INTRINSICS=1 \
	-DWASM_ENABLE_QUICK_AOT_ENTRY=1 \
	-DWASM_ENABLE_WORD_ALIGN_READ=0 \
	-DWASM_MEM_DUAL_BUS_MIRROR=0 \
	-DWASM_ENABLE_FAST_INTERP=0 \
	-DWASM_ENABLE_INTERP=1 \
	-DWASM_ENABLE_MINI_LOADER=0 \
	-DWASM_ENABLE_SHARED_MEMORY=0 \
	-DWASM_ENABLE_BULK_MEMORY=1 \
	-DWASM_ENABLE_LIBC_BUILTIN=0 \
	-DWASM_CONFIGURABLE_BOUNDS_CHECKS=0 \
	-DWASM_ENABLE_LIBC_WASI=0 \
	-DWASM_ENABLE_MODULE_INST_CONTEXT=1 \
	-DWASM_ENABLE_MULTI_MODULE=1 \
	-DWASM_ENABLE_THREAD_MGR=0 \
	-DWASM_ENABLE_LIB_WASI_THREADS=0 \
	-DWASM_ENABLE_LIB_PTHREAD=0 \
	-DWASM_ENABLE_LIB_PTHREAD_SEMAPHORE=0 \
	-DWASM_DISABLE_HW_BOUND_CHECK=0 \
	-DWASM_DISABLE_STACK_HW_BOUND_CHECK=0 \
	-DWASM_DISABLE_WAKEUP_BLOCKING_OP=1 \
	-DWASM_DISABLE_WRITE_GS_BASE=1 \
	-DWASM_ENABLE_LOAD_CUSTOM_SECTION=1 \
	-DWASM_ENABLE_CUSTOM_NAME_SECTION=1 \
	-DWASM_ENABLE_GLOBAL_HEAP_POOL=0 \
	-DWASM_MEM_ALLOC_WITH_USAGE=0 \
	-DWASM_ENABLE_SPEC_TEST=0 \
	-DWASM_ENABLE_REF_TYPES=1 \
	-DWASM_ENABLE_TAIL_CALL=1 \
	-DWASM_ENABLE_EXCE_HANDLING=0 \
	-DWASM_ENABLE_TAGS=0 \
	-DWASM_ENABLE_WAMR_COMPILER=1 \
	-DWASM_ENABLE_SIMD=1 \
	-DWASM_HAVE_MREMAP=1 \
	-DWASM_CONFIGURABLE_BOUNDS_CHECKS=0 \
	-DWASM_ENABLE_AOT_STACK_FRAME=1 \
	-DWASM_ENABLE_DUMP_CALL_STACK=1 \
	-DWASM_ENABLE_HEAP_AUX_STACK_ALLOCATION=1 \
	-DWASM_ENABLE_MEMORY64=0

ALL_INCLUDES = \
	-Isrc \
	-I$(WAMR_CORE_ROOT) \
	-I$(WAMR_IWASM_ROOT)/include \
	-I$(WAMR_IWASM_ROOT)/common \
	-I$(WAMR_IWASM_ROOT)/aot \
	-I$(WAMR_IWASM_ROOT)/compilation \
	-I$(WAMR_IWASM_ROOT)/interpreter \
	-I$(WAMR_SHARED_ROOT)/include \
	-I$(WAMR_SHARED_ROOT)/platform/include \
	-I$(WAMR_SHARED_ROOT)/platform/linux \
	-I$(WAMR_SHARED_ROOT)/utils \
	-I$(WAMR_SHARED_ROOT)/mem-alloc \
	-I$(LLHTTP_SRC)/include

ifeq ($(DEBUG),1)
	WAMR_DEFINES += \
		-DWASM_ENABLE_PERF_PROFILING=0 \
		-DWASM_ENABLE_GC_PERF_PROFILING=0 \
		-DWASM_ENABLE_LINUX_PERF=1 \
		-DWASM_ENABLE_MEMORY_PROFILING=1 \
		-DWASM_ENABLE_MEMORY_TRACING=1 \
		-DWASM_ENABLE_DEBUG_AOT=1
	OBJS += \
		$(WAMR_IWASM_ROOT)/aot/aot_perf_map.o \
		$(WAMR_IWASM_ROOT)/aot/debug/elf_parser.o \
		$(WAMR_IWASM_ROOT)/aot/debug/jit_debug.o \
		$(WAMR_IWASM_ROOT)/compilation/debug/dwarf_extractor.o
	ALL_INCLUDES += \
		-I$(WAMR_IWASM_ROOT)/aot/debug \
		-I$(WAMR_IWASM_ROOT)/compilation/debug
	SHLIB_LINK += -llldb
	PG_CFLAGS += -g -O0
	PG_CXXFLAGS += -g -O0
endif

ifeq ($(GC),1)
	WAMR_DEFINES += \
		-DWASM_ENABLE_GC=1 \
		-DWASM_GC_MANUALLY=0
	OBJS += \
		$(WAMR_SHARED_ROOT)/mem-alloc/ems/ems_gc.o \
		$(WAMR_IWASM_ROOT)/common/gc/gc_common.o \
		$(WAMR_IWASM_ROOT)/common/gc/gc_type.o \
		$(WAMR_IWASM_ROOT)/common/gc/gc_object.o \
		$(WAMR_IWASM_ROOT)/compilation/aot_emit_gc.o
	ALL_INCLUDES += \
		-I$(WAMR_IWASM_ROOT)/common/gc
endif

ifeq ($(BUNDLE_LLVM),1)
	ALL_INCLUDES += \
		-I$(LLVM_BUILD_DIR)/include
	SHLIB_LINK += \
		-Wl,--whole-archive \
		$(LLVM_BUILD_DIR)/lib/*.a \
		-Wl,--no-whole-archive
else
	SHLIB_LINK += -lLLVM
endif

ifeq ($(SQL_BACKDOOR),1)
	WAMR_DEFINES += -DRUSTICA_SQL_BACKDOOR=1
endif

PG_CFLAGS += \
	-Wno-vla \
	-Wno-int-conversion \
	-Wno-declaration-after-statement \
	-Wno-missing-prototypes \
	-Wno-implicit-function-declaration

PG_CPPFLAGS += $(WAMR_DEFINES) $(ALL_INCLUDES)
PG_CXXFLAGS += -fno-rtti

SHLIB_LINK += -lstdc++ \
	$(WAMR_IWASM_ROOT)/common/arch/invokeNative_em64_simd.o

EXTENSION = rustica-wamr
DATA = sql/rustica-wamr--1.0.sql

ifeq ($(DEV),1)
include $(DEV_PG_INSTALL)/.stub
PG_CONFIG = $(DEV_PG_INSTALL)/bin/pg_config
else
PG_CONFIG = pg_config
endif
PGXS := $(shell $(PG_CONFIG) --pgxs)

include $(PGXS)
# PGXS config end

# WAMR source
$(WAMR_TARBALL):
	mkdir -p $(VENDOR_DIR)
	wget -O $(WAMR_TARBALL) \
		"https://github.com/bytecodealliance/wasm-micro-runtime/archive/refs/tags/WAMR-$(WAMR_VERSION).tar.gz"

$(WAMR_DIR)/.stub: $(WAMR_TARBALL)
	mkdir -p $(WAMR_DIR)
	tar xvf $(WAMR_TARBALL) -C $(WAMR_DIR) --strip-components=1
	patch -p1 -d $(WAMR_DIR) < patches/0001-Support-circular-calls-when-multi-module-enabled.patch
	patch -p1 -d $(WAMR_DIR) < patches/0002-wamrc-allow-building-without-DUMP_CALL_STACK.patch
	patch -p1 -d $(WAMR_DIR) < patches/0003-fix-parameter-handling-in-wasm_loader-with-GC-enable.patch
	patch -p1 -d $(WAMR_DIR) < patches/0004-Tweak-submodule-loading-hooks.patch
	patch -p1 -d $(WAMR_DIR) < patches/0005-Support-custom-global-resolver.patch
	echo > $(WAMR_DIR)/.stub
ifeq ($(BUNDLE_LLVM),1)
	cd $(WAMR_DIR)/wamr-compiler && python3 -m pip install -r ../build-scripts/requirements.txt && python3 ../build-scripts/build_llvm.py
endif

include $(WAMR_DIR)/.stub

# Include ASM manually because LLVM can't generate bytecode from ASM
$(WAMR_IWASM_ROOT)/common/arch/invokeNative_em64_simd.s: $(WAMR_DIR)/.stub

%.o : %.s
	@if test ! -d $(DEPDIR); then mkdir -p $(DEPDIR); fi
	$(COMPILE.c) -o $@ $< -MMD -MP -MF $(DEPDIR)/$(*F).Po

all-shared-lib: $(WAMR_IWASM_ROOT)/common/arch/invokeNative_em64_simd.o

# llhttp source
$(LLHTTP_TARBALL):
	mkdir -p $(VENDOR_DIR)
	wget -O $(LLHTTP_TARBALL) \
		"https://github.com/nodejs/llhttp/archive/refs/tags/release/v$(LLHTTP_VERSION).tar.gz"

$(LLHTTP_SRC)/.stub: $(LLHTTP_TARBALL)
	mkdir -p $(LLHTTP_SRC)
	tar xvf $(LLHTTP_TARBALL) -C $(LLHTTP_SRC) --strip-components=1
	echo > $(LLHTTP_SRC)/.stub

include $(LLHTTP_SRC)/.stub

# Development commands
ifeq ($(DEV),1)
$(DEV_PG_TARBALL):
	mkdir -p $(VENDOR_DIR)
	wget -O $(DEV_PG_TARBALL) \
		"https://ftp.postgresql.org/pub/source/v$(DEV_PG_VERSION)/postgresql-$(DEV_PG_VERSION).tar.gz"

$(DEV_PG_SRC): $(DEV_PG_TARBALL)
	mkdir -p $(DEV_PG_SRC)
	tar xvf $(DEV_PG_TARBALL) -C $(DEV_PG_SRC) --strip-components=1

CLEAN_ENV = env -i PATH=$(PATH) HOME=$(HOME) USER=$(USER) CFLAGS=-O0
$(DEV_PG_INSTALL): $(DEV_PG_SRC)
	mkdir -p $(DEV_PG_INSTALL)
	$(eval DEV_PG_INSTALL_PREFIX := $(shell realpath $(DEV_PG_INSTALL)))
	cd $(DEV_PG_SRC) && \
		$(CLEAN_ENV) ./configure --prefix $(DEV_PG_INSTALL_PREFIX) --with-uuid=e2fs --enable-debug && \
		$(CLEAN_ENV) $(MAKE) -j $(shell nproc) && \
		$(CLEAN_ENV) $(MAKE) install && \
		$(CLEAN_ENV) $(MAKE) -C contrib install

$(DEV_PG_INSTALL)/.stub: $(DEV_PG_INSTALL)
	echo > $(DEV_PG_INSTALL)/.stub

$(DEV_PG_DATA):
	$(DEV_PG_INSTALL)/bin/initdb -D $(DEV_PG_DATA)
	echo "shared_preload_libraries = 'rustica-wamr.so'" >> $(DEV_PG_DATA)/postgresql.conf
	echo "rustica.database = 'postgres'" >> $(DEV_PG_DATA)/postgresql.conf

%.bc:
	@true

.PHONY: stop
stop:
	@$(DEV_PG_INSTALL)/bin/pg_ctl -D $(DEV_PG_DATA) -l $(DEV_PG_LOG) stop 2>/dev/null || true

.PHONY: reload
reload: $(DEV_PG_INSTALL) $(DEV_PG_DATA) stop install
	$(DEV_PG_INSTALL)/bin/pg_ctl -D $(DEV_PG_DATA) -l $(DEV_PG_LOG) start || tail $(DEV_PG_LOG)

.PHONY: run
run: $(DEV_PG_INSTALL) $(DEV_PG_DATA) stop install
	$(DEV_PG_INSTALL)/bin/postgres -D $(DEV_PG_DATA)

endif

# All other commands
.PHONY: format
format:
	find src -name '*.h' -o -name "*.c" | xargs clang-format -i

.PHONY: nuke
nuke: clean
	rm -rf $(VENDOR_DIR)
