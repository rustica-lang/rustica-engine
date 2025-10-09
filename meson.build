project(
    'rustica-engine', 'c',
    version : '1.0.0',
    default_options : []
)

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
    zlib = subproject('zlib', default_options: ['default_library=static'])
    zlib_a = zlib.build('zlib')
endif

# Include PostgreSQL subproject

pg = subproject(
    'postgresql',
    default_options: [
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

    # https://github.com/bytecodealliance/wasm-micro-runtime/issues/4640
    'CMAKE_C_FLAGS': '-fzero-init-padding-bits=unions -Wno-incompatible-pointer-types',
    'WAMR_BH_VPRINTF': 'pg_log_vprintf',
    'WAMR_BH_LOG': 'pg_bh_log',
})
if get_option('llvm_dir') != ''
    wamr_var.add_cmake_defines({
        'LLVM_DIR': get_option('llvm_dir'),
    })
elif get_option('embed_libs')
    # Tell WAMR to link LLVM with our static zlib
    wamr_defines = {
        'LLVM_ENABLE_ZLIB:STRING': 'FORCE_ON',
        'ZLIB_LIBRARY': zlib_a,
        'ZLIB_INCLUDE_DIR': zlib.get_variable('depinc').to_list()[0],
        'ZLIB_USE_STATIC_LIBS': 'ON',
        'LLVM_ENABLE_ZSTD:STRING': 'OFF',
    }
    # Build LLVM with the right zlib too
    llvm_cmake_flags = []
    foreach k, v : wamr_defines
        llvm_cmake_flags += ['-D' + k + '=' + v]
    endforeach
    wamr_var.add_cmake_defines(wamr_defines + {
        'LLVM_CMAKE_FLAGS': ' '.join(llvm_cmake_flags),
    })
endif
wamr = cmake.subproject('wamr', options: wamr_var)
vmlib = wamr.dependency('vmlib')

uncommon_shared_lib = shared_library('uncommon_shared',
    wamr.get_cmake_variable('UNCOMMON_SHARED_SOURCE'),
    dependencies: [vmlib],
    c_args: ['-DBH_MALLOC=palloc', '-DBH_FREE=pfree'],
)

# Build the rustica-engine executable

executable('rustica-engine',
    [
        'src/rustica/main.c',
        'src/rustica/env.c',
        'src/rustica/moontest.c',
    ],
    sources: pg.get_variable('post_export_backend_sources'),
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
    dependencies: backend_build_deps + [vmlib, pg.get_variable('uuid')],
    kwargs: pg.get_variable('default_bin_args'),
    c_args: wamr.get_cmake_definitions('-DWASM'),
)
