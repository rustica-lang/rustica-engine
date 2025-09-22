#!/usr/bin/env python3
# -*- coding: utf-8 -*-
from __future__ import annotations
import re
import sys
import typing as T

from mesonbuild.interpreterbase import noKwargs, typed_pos_args
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


if __name__ == "__main__":
    cmake.CMakeSubproject = CMakeSubproject
    sys.argv[0] = re.sub(r"(-script\.pyw|\.exe)?$", "", sys.argv[0])
    sys.exit(main())
