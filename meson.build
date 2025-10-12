# SPDX-FileCopyrightText: 2025 燕几（北京）科技有限公司
# SPDX-License-Identifier: Apache-2.0 OR MulanPSL-2.0

project(
    'rustica-engine', 'c',
    version : '1.0.0',
    default_options : []
)

fs = import('fs')

if get_option('build_zlib_only')
    subproject('zlib', default_options: ['default_library=static'])
    subdir_done()
elif get_option('build_llvm_only')
    subproject('llvm', default_options: ['zlib_dir=' + get_option('zlib_dir')])
    subdir_done()
endif

if get_option('embed_libs')
    add_project_arguments('-static-libstdc++', '-static-libgcc', language: ['c', 'cpp'])
    subproject('lz4', default_options: ['default_library=static'])
    subproject(
        'util-linux',
        default_options: [
            'default_library=static',
            'build-libblkid=disabled',
            'build-liblastlog2=disabled',
            'build-libsmartcols=disabled',
            'build-uuidd=disabled',
            'cryptsetup=disabled',
            'build-plymouth-support=disabled',
        ],
    )
    subproject('icu', default_options: ['default_library=static'])
    subproject('openssl', default_options: ['default_library=static'])
endif

# Include PostgreSQL subproject

pg = subproject(
    'postgresql',
    default_options: [
        'prefix=postgres',
        'docs=disabled',      # we don't need docs
        'readline=disabled',  # for psql
        'zlib=disabled',      # for pgdump/pgrestore
        'libxml=disabled',    # for XML type
        'libxslt=disabled',   # for XML transformations
        'systemd=disabled',   # for systemd support
        'pam=disabled',       # for PAM auth
        'gssapi=disabled',    # for GSSAPI auth
        'ldap=disabled',      # for LDAP lookup
        'zstd=disabled',      # for WAL compression
        'plperl=disabled',
        'plpython=disabled',
        'pltcl=disabled',
        'uuid=e2fs',
    ],
)

add_project_arguments(pg.get_variable('cflags'), language: ['c'])
add_project_arguments(pg.get_variable('cppflags'), language: ['c'])
add_project_arguments(pg.get_variable('cflags_warn'), language: ['c'])
add_project_arguments(pg.get_variable('cxxflags'), language: ['cpp'])
add_project_arguments(pg.get_variable('cppflags'), language: ['cpp'])
add_project_arguments(pg.get_variable('cxxflags_warn'), language: ['cpp'])
add_project_link_arguments(pg.get_variable('ldflags'), language: ['c', 'cpp'])

backend_link_with = pg.get_variable('backend_link_with')
backend_build_deps = pg.get_variable('backend_build_deps')

backend_sources = []
foreach src : pg.get_variable('backend_sources')
  if not src.full_path().endswith('src/backend/main/main.c')
    backend_sources += [src]
  endif
endforeach

postgres_pure_lib = static_library('postgres_lib',
    backend_sources + pg.get_variable('timezone_sources') + pg.get_variable('generated_backend_sources'),
    link_whole: backend_link_with,
    dependencies: backend_build_deps,
    c_pch: pg.get_variable('pch_postgres_h'),
    kwargs: pg.get_variable('internal_lib_args'),
)

# Include WAMR subproject

cc = meson.get_compiler('c')
llvm_inc_flag = '-I' + (get_option('llvm_dir') / 'include')
wamr_c_flags = ['-Wno-incompatible-pointer-types', llvm_inc_flag]
# https://github.com/bytecodealliance/wasm-micro-runtime/issues/4640
if cc.has_argument('-fzero-init-padding-bits=unions')
  wamr_c_flags += ['-fzero-init-padding-bits=unions']
endif

cmake = import('cmake')
wamr_var = cmake.subproject_options()
wamr_var.add_cmake_defines({
    'WAMR_BUILD_GC': 1,
    'WAMR_BUILD_LIBC_BUILTIN': 0,
    'WAMR_BUILD_LIBC_WASI': 0,
    'WAMR_BUILD_DUMP_CALL_STACK': 1,
    'WAMR_BUILD_AOT_STACK_FRAME': 1,
    'WAMR_BUILD_LOAD_CUSTOM_SECTION': 1,
    'WAMR_BUILD_CUSTOM_NAME_SECTION': 1,
    'WAMR_BUILD_TAIL_CALL': 1,
    'WAMR_BUILD_EXTENDED_CONST_EXPR': 1,
    'WAMR_BUILD_EXCE_HANDLING': 1,
    'WAMR_BUILD_JIT': 1,

    'CMAKE_C_FLAGS': ' '.join(wamr_c_flags),
    'CMAKE_CXX_FLAGS': llvm_inc_flag,
    'WAMR_BH_VPRINTF': 'pg_log_vprintf',
    'WAMR_BH_LOG': 'pg_bh_log',
    'LLVM_DIR': get_option('llvm_dir') / 'lib/cmake/llvm',
})
if get_option('zlib_dir') != ''
    zlib_prefix = get_option('zlib_dir')
    wamr_var.add_cmake_defines(
        {
            'ZLIB_LIBRARY': zlib_prefix / 'lib' / 'libz.a',
            'ZLIB_INCLUDE_DIR': zlib_prefix / 'include',
        }
    )
endif
wamr = cmake.subproject('wamr', options: wamr_var)
vmlib = wamr.dependency('vmlib')

uncommon_shared_lib = shared_library('uncommon_shared',
    wamr.get_cmake_variable('UNCOMMON_SHARED_SOURCE'),
    dependencies: [vmlib],
    c_args: ['-DBH_MALLOC=palloc', '-DBH_FREE=pfree'],
    build_by_default: false,
    install: false,
)

# Build the Rustica Engine

rustica_shared = files(
    'src/rustica/env.c',
)
rustica_deps = [vmlib, pg.get_variable('uuid')]
rustica_cargs = wamr.get_cmake_definitions('-DWASM')

executable('rustica-engine',
    [
        'src/rustica/main.c',
        'src/rustica/moontest.c',
    ],
    sources: rustica_shared + pg.get_variable('post_export_backend_sources'),
    objects: [
        postgres_pure_lib.extract_all_objects(recursive: false),
        uncommon_shared_lib.extract_all_objects(recursive: false),
        pg.get_variable('uuid_ossp').extract_all_objects(recursive: false),
    ],
    include_directories: 'src',
    link_args: pg.get_variable('backend_link_args') + (
        get_option('embed_libs') ? ['-static-libstdc++', '-static-libgcc'] : []
    ),
    link_with: backend_link_with,
    link_depends: pg.get_variable('backend_link_depends'),
    export_dynamic: true,
    implib: 'postgres',
    dependencies: backend_build_deps + rustica_deps,
    kwargs: pg.get_variable('default_bin_args'),
    c_args: rustica_cargs,
    install_tag: 'standalone',
)

pg_mod_args = pg.get_variable('pg_mod_args')
shared_module(
    'rustica-engine',
    'src/rustica/extension.c',
    sources: rustica_shared,
    include_directories: 'src',
    c_args: rustica_cargs,
    install_tag: 'extension',
    kwargs: pg_mod_args + {
        'dependencies': rustica_deps + [pg_mod_args['dependencies']],
    },
)
install_data(
    'rustica-engine.control',
    'sql/rustica-engine--1.0.sql',
    install_tag: 'extension',
    kwargs: pg.get_variable('contrib_data_args'),
)
