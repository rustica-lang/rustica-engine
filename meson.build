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
            'tinfo=disabled',
            'ncursesw=disabled',
            'ncurses=disabled',
            'slang=disabled',
            'cryptsetup=disabled',
            'cryptsetup-dlopen=disabled',
            'zlib=disabled',
            'readline=disabled',
            'nls=disabled',
            'libutil=disabled',
            'libutempter=disabled',
            'libpcre2-posix=disabled',
            'libuser=disabled',
            'selinux=disabled',
            'audit=disabled',
            'smack=disabled',
            'magic=disabled',
            'econf=disabled',
            'systemd=disabled',
            'sysvinit=disabled',
            'btrfs=disabled',
            'widechar=disabled',
            'translate-docs=disabled',
            'cryptsetup=disabled',
            'build-plymouth-support=disabled',
            'build-python=disabled',
            'python=disabled',
            'build-libblkid=disabled',
            'build-liblastlog2=disabled',
            'build-pam-lastlog2=disabled',
            'build-libmount=disabled',
            'build-libsmartcols=disabled',
            'build-libfdisk=disabled',
            'build-fdisks=disabled',
            'build-mount=disabled',
            'build-swapon=disabled',
            'build-swapoff=disabled',
            'build-chcpu=disabled',
            'build-losetup=disabled',
            'build-zramctl=disabled',
            'build-lsns=disabled',
            'build-mkfs=disabled',
            'build-fsck=disabled',
            'build-partx=disabled',
            'build-script=disabled',
            'build-scriptutils=disabled',
            'build-bits=disabled',
            'build-col=disabled',
            'build-colcrt=disabled',
            'build-colrm=disabled',
            'build-rev=disabled',
            'build-uuidd=disabled',
            'build-choom=disabled',
            'build-isosize=disabled',
            'build-waitpid=disabled',
            'build-wipefs=disabled',
            'build-mountpoint=disabled',
            'build-fallocate=disabled',
            'build-unshare=disabled',
            'build-nsenter=disabled',
            'build-setpriv=disabled',
            'build-hardlink=disabled',
            'build-eject=disabled',
            'build-agetty=disabled',
            'build-cramfs=disabled',
            'build-bfs=disabled',
            'build-minix=disabled',
            'build-fdformat=disabled',
            'build-blockdev=disabled',
            'build-hwclock=disabled',
            'build-lslogins=disabled',
            'build-wdctl=disabled',
            'build-cal=disabled',
            'build-logger=disabled',
            'build-look=disabled',
            'build-mcookie=disabled',
            'build-namei=disabled',
            'build-whereis=disabled',
            'build-lsblk=disabled',
            'build-lslocks=disabled',
            'build-findmnt=disabled',
            'build-lsfd=disabled',
            'build-switch_root=disabled',
            'build-pivot_root=disabled',
            'build-flock=disabled',
            'build-lsmem=disabled',
            'build-lsirq=disabled',
            'build-irqtop=disabled',
            'build-chmem=disabled',
            'build-ipcmk=disabled',
            'build-ipcrm=disabled',
            'build-ipcs=disabled',
            'build-rfkill=disabled',
            'build-tunelp=disabled',
            'build-fstrim=disabled',
            'build-dmesg=disabled',
            'build-ctrlaltdel=disabled',
            'build-exch=disabled',
            'build-fsfreeze=disabled',
            'build-blkdiscard=disabled',
            'build-blkzone=disabled',
            'build-blkpr=disabled',
            'build-ldattach=disabled',
            'build-rtcwake=disabled',
            'build-setarch=disabled',
            'build-kill=disabled',
            'build-last=disabled',
            'build-utmpdump=disabled',
            'build-line=disabled',
            'build-mesg=disabled',
            'build-raw=disabled',
            'build-rename=disabled',
            'build-vipw=disabled',
            'build-newgrp=disabled',
            'build-chfn-chsh=disabled',
            'build-login=disabled',
            'build-nologin=disabled',
            'build-sulogin=disabled',
            'build-su=disabled',
            'build-runuser=disabled',
            'build-ul=disabled',
            'build-more=disabled',
            'build-pg=disabled',
            'build-pipesz=disabled',
            'build-fadvise=disabled',
            'build-enosys=disabled',
            'build-lsclocks=disabled',
            'build-getopt=disabled',
            'build-setterm=disabled',
            'build-schedutils=disabled',
            'build-wall=disabled',
            'build-write=disabled',
            'build-bash-completion=disabled',
            'build-pylibmount=disabled',
            'build-hexdump=disabled',
            'build-findfs=disabled',
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
wamr_c_flags = [
    '-DWASM_SUPPORT_NUL_IN_STRING=1',
    '-Wno-incompatible-pointer-types',
    llvm_inc_flag,
]
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
    'src/rustica/bh_log_to_pg.c',
    'src/rustica/datatypes.c',
    'src/rustica/adt/clock.c',
    'src/rustica/adt/stringbuilder.c',
    'src/rustica/adt/text.c',
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
    'rustica-engine.so',
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
