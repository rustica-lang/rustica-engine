#!/usr/bin/env python3
# -*- coding: utf-8 -*-
from __future__ import annotations
import copy
import pathlib
import re
import sys
import typing as T

from mesonbuild import mlog
from mesonbuild.interpreter import interpreter as ii
from mesonbuild.interpreter import interpreterobjects
from mesonbuild.interpreterbase import noKwargs, noPosargs, typed_pos_args, InterpreterObject
from mesonbuild.cmake import interpreter
from mesonbuild.mesonmain import main
from mesonbuild.modules import cmake
from mesonbuild.mesonlib import File
from mesonbuild.utils.universal import Popen_safe

if T.TYPE_CHECKING:
    from mesonbuild.modules import ModuleState
    from mesonbuild.interpreter import SubprojectHolder
    from mesonbuild.interpreterbase import TYPE_kwargs, TYPE_var


class CMakeSubproject(cmake.CMakeSubproject):
    def __init__(self, subp: SubprojectHolder):
        super().__init__(subp)
        self.methods["get_cmake_variable"] = self.get_cmake_variable
        self.methods["get_cmake_definitions"] = self.get_cmake_definitions

    @noKwargs
    @typed_pos_args("cmake.subproject.get_cmake_variable", str, optargs=[str])
    def get_cmake_variable(
        self,
        state: ModuleState,
        args: T.Tuple[str, T.Optional[str]],
        kwargs: TYPE_kwargs,
    ) -> T.Union[TYPE_var, InterpreterObject]:
        rv = self.cm_interpreter.trace.get_cmake_var(args[0])
        return [File.from_absolute_file(x) for x in rv]

    @noKwargs
    @typed_pos_args("cmake.subproject.get_cmake_definitions", str)
    def get_cmake_definitions(
        self,
        state: ModuleState,
        args: T.Tuple[str],
        kwargs: TYPE_kwargs,
    ) -> T.Union[TYPE_var, InterpreterObject]:
        return [d for d in self.cm_interpreter.trace.definitions if d.startswith(args[0])]


class CMakeTraceParser(interpreter.CMakeTraceParser):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.definitions = []
        self.functions["add_definitions"] = self.add_definitions

    def add_definitions(self, tline: CMakeTraceLine) -> None:
        self.definitions.extend(tline.args)


class SubprojectHolder(ii.SubprojectHolder):
    @noKwargs
    @typed_pos_args("subproject.build", str)
    @InterpreterObject.method('build')
    def build_method(self, args: T.List[TYPE_var], kwargs: TYPE_kwargs) -> str:
        backend = copy.copy(self.held_object.backend)
        for k, v in list(backend.__dict__.items()):
            try:
                backend.__dict__[k] = copy.copy(v)
            except Exception:
                pass
        env = backend.environment
        builddir = pathlib.Path(env.build_dir).with_name("build-staging")
        env.build_dir = str(builddir)
        (builddir / 'meson-private').mkdir(parents=True, exist_ok=True)
        mlog.log(
            "Building subproject",
            mlog.bold(self.held_object.subproject),
            "for target",
            mlog.bold(args[0]),
            "in",
            mlog.bold(str(builddir)),
        )
        env.dump_coredata()
        backend.generate(False, None)
        rv = backend.get_target_filename_abs(
            self.held_object.variables[args[0]]._target_object
        )
        target = pathlib.Path(rv).relative_to(builddir)
        p, o, e = Popen_safe(['ninja', target], cwd=builddir)
        if p.returncode != 0:
            mlog.error("Subproject build failed:")
            mlog.log('--- stdout ---')
            mlog.log(o)
            mlog.log('--- stderr ---')
            mlog.log(e)
            mlog.log('')
            raise RuntimeError("Subproject build failed")
        else:
            mlog.log("Subproject build successful")
        return rv


class IncludeDirsHolder(interpreterobjects.IncludeDirsHolder):
    @noKwargs
    @noPosargs
    @InterpreterObject.method('to_list')
    def to_list_method(self, args: T.List[TYPE_var], kwargs: TYPE_kwargs) -> T.List[str]:
        curdir = pathlib.Path(self.held_object.curdir)
        return [str((curdir / inc).resolve()) for inc in self.held_object.incdirs]


if __name__ == "__main__":
    cmake.CMakeSubproject = CMakeSubproject
    interpreter.CMakeTraceParser = CMakeTraceParser
    ii.SubprojectHolder = SubprojectHolder
    interpreterobjects.IncludeDirsHolder = IncludeDirsHolder
    sys.argv[0] = re.sub(r"(-script\.pyw|\.exe)?$", "", sys.argv[0])
    sys.exit(main())
