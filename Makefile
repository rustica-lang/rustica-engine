VENDOR_DIR = ./vendor
WAMR_VERSION = 2.1.0
WAMR_DIR = $(VENDOR_DIR)/wamr-$(WAMR_VERSION)
WAMR_CORE_ROOT = $(WAMR_DIR)/core
WAMR_IWASM_ROOT = $(WAMR_CORE_ROOT)/iwasm
WAMR_SHARED_ROOT = $(WAMR_CORE_ROOT)/shared
DEV_PG_VERSION = 16.3
DEV_PG_SRC = $(VENDOR_DIR)/pg-$(DEV_PG_VERSION)
DEV_PG_INSTALL := $(shell realpath $(VENDOR_DIR)/pg-$(DEV_PG_VERSION)-install)
DEV_PG_DATA := $(shell realpath $(VENDOR_DIR)/pg-$(DEV_PG_VERSION)-data)
DEV_PG_LOG := $(shell realpath $(VENDOR_DIR)/pg-$(DEV_PG_VERSION).log)
DEV = 0

MODULE_big = rustica-wamr

OBJS = \
	$(WAMR_SHARED_ROOT)/platform/linux/platform_init.o \
	$(WAMR_SHARED_ROOT)/platform/common/posix/posix_thread.o \
	$(WAMR_SHARED_ROOT)/platform/common/posix/posix_time.o \
	$(WAMR_SHARED_ROOT)/platform/common/posix/posix_malloc.o \
	$(WAMR_SHARED_ROOT)/platform/common/posix/posix_memmap.o \
	$(WAMR_SHARED_ROOT)/platform/common/memory/mremap.o \
	$(WAMR_SHARED_ROOT)/mem-alloc/mem_alloc.o \
	$(WAMR_SHARED_ROOT)/mem-alloc/ems/ems_kfc.o \
	$(WAMR_SHARED_ROOT)/mem-alloc/ems/ems_alloc.o \
	$(WAMR_SHARED_ROOT)/mem-alloc/ems/ems_hmu.o \
	$(WAMR_SHARED_ROOT)/mem-alloc/ems/ems_gc.o \
	$(WAMR_SHARED_ROOT)/utils/bh_assert.o \
	$(WAMR_SHARED_ROOT)/utils/bh_bitmap.o \
	$(WAMR_SHARED_ROOT)/utils/bh_common.o \
	$(WAMR_SHARED_ROOT)/utils/bh_hashmap.o \
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
	$(WAMR_IWASM_ROOT)/common/gc/gc_common.o \
	$(WAMR_IWASM_ROOT)/common/gc/gc_type.o \
	$(WAMR_IWASM_ROOT)/common/gc/gc_object.o \
	$(WAMR_IWASM_ROOT)/common/arch/invokeNative_em64.o \
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
    $(WAMR_IWASM_ROOT)/compilation/aot_emit_gc.o \
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
	src/module.o \
	src/gucs.o \
	src/event_set.o \
	src/worker.o \
	src/master.o

#	$(WAMR_SHARED_ROOT)/utils/uncommon/bh_read_file.o \

WAMR_DEFINES = \
	-DBUILD_TARGET_X86_64 \
	-DBH_MALLOC=wasm_runtime_malloc \
	-DBH_FREE=wasm_runtime_free \
	-DWASM_ENABLE_LOG=1 \
	-DWASM_ENABLE_AOT=1 \
	-DWASM_ENABLE_DEBUG_AOT=0 \
	-DWASM_ENABLE_QUICK_AOT_ENTRY=0 \
	-DWASM_ENABLE_WORD_ALIGN_READ=0 \
	-DWASM_MEM_DUAL_BUS_MIRROR=0 \
	-DWASM_ENABLE_FAST_INTERP=0 \
	-DWASM_ENABLE_INTERP=1 \
	-DWASM_ENABLE_MINI_LOADER=1 \
	-DWASM_ENABLE_SHARED_MEMORY=0 \
	-DWASM_ENABLE_BULK_MEMORY=1 \
	-DWASM_ENABLE_AOT_STACK_FRAME=1 \
	-DWASM_ENABLE_PERF_PROFILING=1 \
	-DWASM_ENABLE_GC_PERF_PROFILING=0 \
	-DWASM_ENABLE_MEMORY_PROFILING=1 \
	-DWASM_ENABLE_MEMORY_TRACING=1 \
	-DWASM_ENABLE_DUMP_CALL_STACK=1 \
	-DWASM_ENABLE_LIBC_BUILTIN=0 \
	-DWASM_CONFIGURABLE_BOUNDS_CHECKS=0 \
	-DWASM_ENABLE_LIBC_WASI=0 \
	-DWASM_ENABLE_MODULE_INST_CONTEXT=1 \
	-DWASM_ENABLE_MULTI_MODULE=0 \
	-DWASM_ENABLE_THREAD_MGR=0 \
	-DWASM_ENABLE_LIB_WASI_THREADS=0 \
	-DWASM_ENABLE_GC=1 \
	-DWASM_GC_MANUALLY=0 \
	-DWASM_ENABLE_LIB_PTHREAD=0 \
	-DWASM_ENABLE_LIB_PTHREAD_SEMAPHORE=0 \
	-DWASM_DISABLE_HW_BOUND_CHECK=1 \
	-DWASM_DISABLE_STACK_HW_BOUND_CHECK=1 \
	-DWASM_DISABLE_WAKEUP_BLOCKING_OP=1 \
	-DWASM_ENABLE_LOAD_CUSTOM_SECTION=0 \
	-DWASM_ENABLE_CUSTOM_NAME_SECTION=0 \
	-DWASM_ENABLE_GLOBAL_HEAP_POOL=0 \
	-DWASM_MEM_ALLOC_WITH_USAGE=0 \
	-DWASM_ENABLE_SPEC_TEST=0 \
	-DWASM_ENABLE_REF_TYPES=1 \
	-DWASM_ENABLE_TAIL_CALL=1 \
	-DWASM_ENABLE_EXCE_HANDLING=1 \
	-DWASM_ENABLE_TAGS=1 \
	-DWASM_ENABLE_WAMR_COMPILER=1 \
	-DWASM_ENABLE_SIMD=1 \
	-DWASM_ENABLE_MEMORY64=1

WAMR_INCLUDES = \
	-I$(WAMR_CORE_ROOT) \
	-I$(WAMR_IWASM_ROOT)/include \
	-I$(WAMR_IWASM_ROOT)/common \
	-I$(WAMR_IWASM_ROOT)/common/gc \
	-I$(WAMR_IWASM_ROOT)/aot \
	-I$(WAMR_IWASM_ROOT)/compilation \
	-I$(WAMR_IWASM_ROOT)/interpreter \
	-I$(WAMR_SHARED_ROOT)/include \
	-I$(WAMR_SHARED_ROOT)/platform/include \
	-I$(WAMR_SHARED_ROOT)/platform/linux \
	-I$(WAMR_SHARED_ROOT)/utils \
	-I$(WAMR_SHARED_ROOT)/mem-alloc

#	-I$(WAMR_SHARED_ROOT)/utils/uncommon \

PG_CFLAGS += $(WAMR_DEFINES) $(WAMR_INCLUDES) \
	-Wno-incompatible-pointer-types \
	-Wno-int-conversion \
	-Wno-implicit-function-declaration

PG_CPPFLAGS += $(WAMR_DEFINES) $(WAMR_INCLUDES)

SHLIB_LINK += -lLLVM -lstdc++

EXTENSION = rustica-wamr
DATA = sql/rustica-wamr--1.0.sql

ifeq ($(DEV),1)
PG_CONFIG = $(DEV_PG_INSTALL)/bin/pg_config
PG_CFLAGS += -g
else
PG_CONFIG = pg_config
endif
PGXS := $(shell $(PG_CONFIG) --pgxs)

include $(PGXS)

WAMR_TARBALL = "$(VENDOR_DIR)/wamr-$(WAMR_VERSION).tar.gz"
$(VENDOR_DIR):
	mkdir -p $(VENDOR_DIR)
	wget -O $(WAMR_TARBALL) \
		"https://github.com/bytecodealliance/wasm-micro-runtime/archive/refs/tags/WAMR-$(WAMR_VERSION).tar.gz"
	mkdir -p $(WAMR_DIR)
	tar xvf $(WAMR_TARBALL) -C $(WAMR_DIR) --strip-components=1
	patch -p1 -d vendor/wamr-2.1.0 < patches/0001-Add-aot_emit_aot_file.h.patch
	patch -p1 -d vendor/wamr-2.1.0 < patches/0002-Extract-aot_emit_aot_file_buf_ex-and-expose-friends.patch
	patch -p1 -d vendor/wamr-2.1.0 < patches/0003-Remove-the-exposure-of-AOTObjectData.patch

DEV_PG_TARBALL = "$(VENDOR_DIR)/pg-$(DEV_PG_VERSION).tar.gz"
CLEAN_ENV = env -i PATH=$(PATH) HOME=$(HOME) USER=$(USER)
.PHONY: dev-pg
dev-pg: $(VENDOR_DIR)
	wget -O $(DEV_PG_TARBALL) \
		"https://ftp.postgresql.org/pub/source/v$(DEV_PG_VERSION)/postgresql-$(DEV_PG_VERSION).tar.gz"
	mkdir -p $(DEV_PG_SRC)
	tar xvf $(DEV_PG_TARBALL) -C $(DEV_PG_SRC) --strip-components=1
	mkdir -p $(DEV_PG_INSTALL)
	cd $(DEV_PG_SRC) && \
		$(CLEAN_ENV) ./configure --prefix $(DEV_PG_INSTALL) --with-uuid=e2fs --enable-debug && \
		$(CLEAN_ENV) make -j $(shell nproc) && \
		$(CLEAN_ENV) make install
	$(DEV_PG_INSTALL)/bin/initdb -D $(DEV_PG_DATA)
	echo "shared_preload_libraries = 'rustica-wamr.so'" >> $(DEV_PG_DATA)/postgresql.conf

.PHONY: reload
reload: install
	$(DEV_PG_INSTALL)/bin/pg_ctl -D $(DEV_PG_DATA) -l $(DEV_PG_LOG) stop || true
	$(DEV_PG_INSTALL)/bin/pg_ctl -D $(DEV_PG_DATA) -l $(DEV_PG_LOG) start

.PHONY: clean-vendor
clean-vendor:
	rm -rf $(VENDOR_DIR)

$(WAMR_IWASM_ROOT)/common/arch/invokeNative_em64.o: $(WAMR_IWASM_ROOT)/common/arch/invokeNative_em64.s
	as -o $(WAMR_IWASM_ROOT)/common/arch/invokeNative_em64.o $(WAMR_IWASM_ROOT)/common/arch/invokeNative_em64.s

.PHONY: format
format:
	find src -name '*.h' -o -name "*.c" | xargs clang-format -i

%.bc:
	@true
