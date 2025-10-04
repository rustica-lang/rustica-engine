#!/usr/bin/env python3
# -*- coding: utf-8 -*-
from __future__ import annotations
import re
import sys
import typing as T

from mesonbuild.interpreterbase import noKwargs, typed_pos_args
from mesonbuild.cmake import interpreter
from mesonbuild.mesonmain import main
from mesonbuild.modules import cmake
from mesonbuild.mesonlib import File

if T.TYPE_CHECKING:
    from mesonbuild.modules import ModuleState
    from mesonbuild.interpreter import SubprojectHolder
    from mesonbuild.interpreterbase import TYPE_kwargs, TYPE_var, InterpreterObject


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


if __name__ == "__main__":
    cmake.CMakeSubproject = CMakeSubproject
    interpreter.CMakeTraceParser = CMakeTraceParser
    sys.argv[0] = re.sub(r"(-script\.pyw|\.exe)?$", "", sys.argv[0])
    sys.exit(main())
