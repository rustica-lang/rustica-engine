/*
 * SPDX-FileCopyrightText: 2024 燕几（北京）科技有限公司
 * SPDX-License-Identifier: Apache-2.0 OR MulanPSL-2.0
 */

#ifndef RUSTICA_COMPILER_H
#define RUSTICA_COMPILER_H

#include "postgres.h"
#include "fmgr.h"

Datum rst_compile(PG_FUNCTION_ARGS);

#endif /* RUSTICA_COMPILER_H */
