#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# SPDX-FileCopyrightText: 2025 燕几（北京）科技有限公司
# SPDX-License-Identifier: Apache-2.0 OR MulanPSL-2.0

from __future__ import annotations
import pathlib
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
    from mesonbuild.interpreterbase import InterpreterObject, TYPE_kwargs, TYPE_var


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


class CMakeSubprojectOptions(cmake.CMakeSubprojectOptions):
    def __init__(self):
        super().__init__()
        self.methods["add_extra_cmake_option"] = self.add_extra_cmake_option

    @typed_pos_args('subproject_options.add_extra_cmake_option', str)
    @noKwargs
    def add_extra_cmake_option(
        self,
        state: ModuleState,
        args: T.Tuple[str],
        kwargs: TYPE_kwargs,
    ) -> None:
        self.cmake_options.append(args[0])


class CMakeExecutor(interpreter.CMakeExecutor):
    def call(self, args, *posargs, **kwargs):
        new_args = []
        override_source = None
        for a in args:
            if a.startswith('-S'):
                override_source = a[2:]
            else:
                new_args.append(a)
        if override_source is not None:
            new_args[-1] = str(pathlib.Path(new_args[-1]) / override_source)
        return super().call(new_args, *posargs, **kwargs)


if __name__ == "__main__":
    cmake.CMakeSubproject = CMakeSubproject
    cmake.CMakeSubprojectOptions = CMakeSubprojectOptions
    interpreter.CMakeExecutor = CMakeExecutor
    interpreter.CMakeTraceParser = CMakeTraceParser
    sys.argv[0] = re.sub(r"(-script\.pyw|\.exe)?$", "", sys.argv[0])
    sys.exit(main())
