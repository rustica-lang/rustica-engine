project(
    'rustica-engine', 'c',
    version : '1.0.0',
    default_options : []
)

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

backend_objs = [postgres_pure_lib.extract_all_objects(recursive: false)]

executable('rustica-engine',
    'main.c',
    sources: pg.get_variable('post_export_backend_sources'),
    objects: backend_objs,
    link_args: pg.get_variable('backend_link_args'),
    link_with: backend_link_with,
    link_depends: pg.get_variable('backend_link_depends'),
    export_dynamic: true,
    implib: 'postgres',
    dependencies: backend_build_deps,
    kwargs: pg.get_variable('default_bin_args'),
)
