# <img src="docs/logo.svg" height="48px" align="right">  Rustica Engine - Postgres as an API Server!

[![中文](https://img.shields.io/badge/Zh-中文-informational?logo=data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABQAAAAQCAYAAAAWGF8bAAAAAXNSR0IArs4c6QAAAERlWElmTU0AKgAAAAgAAYdpAAQAAAABAAAAGgAAAAAAA6ABAAMAAAABAAEAAKACAAQAAAABAAAAFKADAAQAAAABAAAAEAAAAABHXVY9AAABc0lEQVQ4EaWSOy8EURTHd+wDEY94JVtgg9BI1B6dQqHiE1CrRasT30DpC2hVQimiFkJWVsSj2U1EsmQH4/ff3CO3WDuDk/zmf8/jnntm7qRSMRZFUQ4WYSimNFmaRlsgq8F83K6WuALyva4mixbc+kfJcGqa7CqU4AjaocNpG5oHsx7qB3EqQRC8K4g/gazAMbFTBdbgL1Zh0w2EbnMVHdMrd4LZNotZmIZJKMAemC2z0MS6oDlYhzOQ6c3yGR5Fec4OGPvEHCmn3np+kfyT51+QH8afcbFLTfjgFVS9tZrpwC4v1k9M39w3NTQrBxSM4127SAmNoBt0Ma3QyHRwGUIYdQUh0+c0wZsLPKKH8AwvoHgNlmABZLtwBdqnP0DD9IEG2If6N0oz5SbYSfW4PYhvgNmUxU1JZGEEAsUyjPmB7lhBA1Xe7NMWpuzXa39fnC7lN1b/mZttSNLQv9XXZs2US9LwzjU5R+/d+n/CBx9I2uELeXrRajeDqHwAAAAASUVORK5CYII=)](README.zh.md)
[![license](https://img.shields.io/badge/license-MulanPSL--2.0-success?logo=opensourceinitiative&logoColor=white)](https://license.coscl.org.cn/MulanPSL2)


Rustica Engine is a PostgreSQL extension that allows you to run WebAssembly
modules in the database securely, turning your database into an API server.

This project is a work in progress and not ready for production use.


## Design Goals

* **Fast**: AoT-compiled WASM (by WAMR/LLVM) and prepared queries
* **Secure**: strictly sandboxed in bgworker processes
* **Native**: PostgreSQL types/Datums directly in the guest
* **Zero-Copy**: operate on network buffers and database pages


## Installation

1. Install system dependencies (Arch Linux)

    ```
    $ sudo pacman -S llvm postgresql
    ```

2. Build and install

    ```
    $ make
    $ sudo make install
    ```


## Development

* Install system dependencies (Arch Linux)

    ```
    $ sudo pacman -S llvm lldb
    ```

* Build extension and run the dev Postgres:

    ```
    $ make run DEV=1 -j $(nproc)
    ```

* When you changed settings in Makefile, rebuild extension files:

    ```
    $ make clean DEV=1
    ```

* When you need to nuke the environment:

    ```
    $ make nuke
    ```


## Testing

1. Convert WASM binary to octets for step 2:

    ```
    $ xxd -p target/wasm-gc/release/build/main/main.wasm | tr -d '\n'
    ```

2. Deploy the WASM application:

    ```
    $ psql -h /tmp postgres
    postgres=# CREATE EXTENSION "rustica-engine" CASCADE;
    postgres=# DELETE FROM rustica.queries;
    postgres=# DELETE FROM rustica.modules;
    postgres=# WITH wasm AS (SELECT '\x0061736d01000000010f035e7801600364007f7f017f600000020c0103656e760473656e6400010304030102020401000503010001060100070a01065f737461727400030801020901000c01010a21030a0020002001200210000b02000b110041004133fb0900004100413310011a0b0b36010133485454502f312e3020323030204f4b0d0a436f6e74656e742d4c656e6774683a2031320d0a0d0a68656c6c6f20776f726c640a'::bytea AS code), compiled AS (SELECT rustica.compile_wasm(code) AS result FROM wasm) INSERT INTO rustica.modules SELECT 'main', code, (result).bin_code, (result).heap_types FROM wasm, compiled RETURNING name;
    ```

3. Invoke the API:

    ```
    $ curl localhost:8080
    ```
