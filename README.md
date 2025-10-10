# <img src="docs/logo.svg" height="48px" align="right">  Rustica Engine - Postgres as an API Server!

[![中文](https://img.shields.io/badge/Zh-中文-informational?logo=data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABQAAAAQCAYAAAAWGF8bAAAAAXNSR0IArs4c6QAAAERlWElmTU0AKgAAAAgAAYdpAAQAAAABAAAAGgAAAAAAA6ABAAMAAAABAAEAAKACAAQAAAABAAAAFKADAAQAAAABAAAAEAAAAABHXVY9AAABc0lEQVQ4EaWSOy8EURTHd+wDEY94JVtgg9BI1B6dQqHiE1CrRasT30DpC2hVQimiFkJWVsSj2U1EsmQH4/ff3CO3WDuDk/zmf8/jnntm7qRSMRZFUQ4WYSimNFmaRlsgq8F83K6WuALyva4mixbc+kfJcGqa7CqU4AjaocNpG5oHsx7qB3EqQRC8K4g/gazAMbFTBdbgL1Zh0w2EbnMVHdMrd4LZNotZmIZJKMAemC2z0MS6oDlYhzOQ6c3yGR5Fec4OGPvEHCmn3np+kfyT51+QH8afcbFLTfjgFVS9tZrpwC4v1k9M39w3NTQrBxSM4127SAmNoBt0Ma3QyHRwGUIYdQUh0+c0wZsLPKKH8AwvoHgNlmABZLtwBdqnP0DD9IEG2If6N0oz5SbYSfW4PYhvgNmUxU1JZGEEAsUyjPmB7lhBA1Xe7NMWpuzXa39fnC7lN1b/mZttSNLQv9XXZs2US9LwzjU5R+/d+n/CBx9I2uELeXrRajeDqHwAAAAASUVORK5CYII=)](README.zh.md)
[![license](https://img.shields.io/badge/license-Apache--2.0-success?logo=opensourceinitiative&logoColor=white)](https://www.apache.org/licenses/LICENSE-2.0)
[![license](https://img.shields.io/badge/license-MulanPSL--2.0-success?logo=opensourceinitiative&logoColor=white)](https://license.coscl.org.cn/MulanPSL2)


Rustica Engine is a PostgreSQL extension that allows you to run WebAssembly
modules in the database securely, turning your database into an API server.

This project is a work in progress and not ready for production use.


## Features

* **Fast**: AoT-compiled WASM (by WAMR/LLVM) and prepared queries
* **Secure**: strictly sandboxed in bgworker processes
* **Native**: PostgreSQL types/Datums directly in the guest
* **Zero-Copy**: operate on network buffers and database pages

Rustica Engine does not use WASI or Component Model, but defines a custom
set of FFIs at a finer granularity, providing optimized bindings for various
PostgreSQL data types and functions.

Currently, the following programming languages can generate WebAssembly
modules suitable for the Rustica Engine:

* Rustica (using MoonGRES backend)
* MoonBit (via MoonGRES extension)

Rustica Engine is also packaged as a standalone command-line application,
statically linked with PostgreSQL backend code, which can run the same
WebAssembly programs without starting a database server.
This is particularly useful for debugging and testing.


## Development

This project is configured with a modified Meson, built by uv, and uses Ninja
as the build system. The `Makefile` provides some convenient command wrappers.

* Install system dependencies (Arch Linux)

    ```
    $ sudo pacman -S make gcc ninja uv llvm18 bison flex
    ```

* Build extension and run the dev Postgres:

    ```
    $ make
    $ make run
    ```


## Testing

1. Install MoonGRES:

    ```
    $ curl ...TBD... | sh
    ```

2. Replace the `rustica-engine` bundled with MoonGRES with the one freshly built:

    ```
    $ ln -sf $(pwd)/install/bin/rustica-engine ~/.moon/bin/rustica-engine
    ```

3. Run MoonBit core test suite:

    ```
    $ cd ~/.moon/lib/core && moon test --target moongres
    ```


## License

This project is dual-licensed under:

* Apache License, Version 2.0
* Mulan Permissive Software License, Version 2

You may choose either license to use this project freely, provided
that you comply with the terms of the chosen license.

`SPDX-License-Identifier: Apache-2.0 OR MulanPSL-2.0`
