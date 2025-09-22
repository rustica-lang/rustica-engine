project(
    'rustica-engine', 'c',
    version : '1.0.0',
    default_options : []
)

# Include PostgreSQL subproject

pg = subproject('postgresql', default_options: ['uuid=e2fs'])

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
    'WAMR_BUILD_LOAD_CUSTOM_SECTION': 1,
    'WAMR_BUILD_CUSTOM_NAME_SECTION': 1,
    'WAMR_BUILD_FAST_INTERP': 0,
    'WAMR_BUILD_EXTENDED_CONST_EXPR': 1,

    # https://github.com/bytecodealliance/wasm-micro-runtime/issues/4640
    'CMAKE_C_FLAGS': '-fzero-init-padding-bits=unions',
})
wamr = cmake.subproject('wamr', options: wamr_var)
vmlib = wamr.dependency('vmlib')

uncommon_shared_lib = shared_library('uncommon_shared',
    wamr.get_cmake_variable('UNCOMMON_SHARED_SOURCE'),
    dependencies: [vmlib],
)

# Build the rustica-engine executable

executable('rustica-engine',
    'main.c',
    sources: pg.get_variable('post_export_backend_sources'),
    objects: [
        postgres_pure_lib.extract_all_objects(recursive: false),
        uncommon_shared_lib.extract_all_objects(recursive: false),
    ],
    link_args: pg.get_variable('backend_link_args'),
    link_with: backend_link_with,
    link_depends: pg.get_variable('backend_link_depends'),
    export_dynamic: true,
    implib: 'postgres',
    dependencies: backend_build_deps + [vmlib],
    kwargs: pg.get_variable('default_bin_args'),
)
