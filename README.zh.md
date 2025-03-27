# <img src="docs/logo.svg" height="48px" align="right">  燕几图引擎——变 PG 为 API 服务器！

[![English](https://img.shields.io/badge/英文-English-informational?logo=data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABsAAAAQCAYAAADnEwSWAAAABGdBTUEAALGPC/xhBQAAADhlWElmTU0AKgAAAAgAAYdpAAQAAAABAAAAGgAAAAAAAqACAAQAAAABAAAAG6ADAAQAAAABAAAAEAAAAACiF0fSAAABJUlEQVQ4EWP8//8/MwMDAysQEw0YGRl/EK0YWSHQsgogJgX8RNZPCpuJFMWUqqWrZSw4XBsCFL+FQ+4/DnEUYWC8gNKCEDB+X8MlgIKVWCJMD64ADwOorwmIHyNhdyDbFYgPAfFXIAaBV0CcCTIG5DNsLo0GKnDEYc8+oGsvQ+UEgLQMkrpYIDsSiJGjRxTInwY07wmuYCxDMgCdmQUUgFmGLheNLoDEzwG5gBFJgFQmNr3ZQEPsgfgImmEquHy2BajwHZpiGPcmjAGk0aPgCDCIp4HkgcHWA6RsQGwoEMEVZ9VATZdgqkig7yKp/YjEBjORIxJdjhw+3tIFVzCGAoPBEo9ta4E+f4NHHqsULstqsKpGCJ4BMkm2jNrBiHAOFhZdLQMA8pKhkQYZiokAAAAASUVORK5CYII=)](README.md)
[![许可](https://img.shields.io/badge/许可-木兰PSLv2-success?logo=opensourceinitiative&logoColor=white)](http://license.coscl.org.cn/MulanPSL2/)


燕几图引擎是一个 PostgreSQL 的扩展程序，使用 WebAssembly
技术，在数据库中直接安全地运行业务逻辑 ，将数据库变成一个 API
服务器。

这个项目还在开发中，请勿用于生产环境。


## 设计目标

* **快速**：使用 WAMR/LLVM，预编译 WASM 和查询语句
* **安全**：多进程模型，严格的隔离设计
* **原生**：镜像 PostgreSQL 的数据和类型
* **零拷贝**：直接访问网络缓冲区和数据库共享内存


## 安装

1. 安装系统依赖 (以 Arch Linux 为例)：

    ```
    $ sudo pacman -S llvm postgresql
    ```

2. 构建并安装：

    ```
    $ make
    $ sudo make install
    ```


## 开发

* 安装系统依赖 (以 Arch Linux 为例)：

    ```
    $ sudo pacman -S llvm lldb
    ```

* 构建扩展程序并运行开发版的 Postgres：

    ```
    $ make run DEV=1 -j $(nproc)
    ```

* 当你修改了 Makefile 中的设置时，重新构建扩展程序：

    ```
    $ make clean DEV=1
    ```

* 彻底清空构建文件：

    ```
    $ make nuke
    ```


## 测试

1. 将 WASM 二进制文件转换为八进制数，以便进行下一步操作：

    ```
    $ xxd -p target/wasm-gc/release/build/main/main.wasm | tr -d '\n'
    ```

2. 部署 WASM 应用程序：

    ```
    $ psql -h /tmp postgres
    postgres=# CREATE EXTENSION "rustica-engine" CASCADE;
    postgres=# DELETE FROM rustica.queries;
    postgres=# DELETE FROM rustica.modules;
    postgres=# WITH wasm AS (SELECT '\x0061736d01000000010f035e7801600364007f7f017f600000020c0103656e760473656e6400010304030102020401000503010001060100070a01065f737461727400030801020901000c01010a21030a0020002001200210000b02000b110041004133fb0900004100413310011a0b0b36010133485454502f312e3020323030204f4b0d0a436f6e74656e742d4c656e6774683a2031320d0a0d0a68656c6c6f20776f726c640a'::bytea AS code), compiled AS (SELECT rustica.compile_wasm(code) AS result FROM wasm) INSERT INTO rustica.modules SELECT 'main', code, (result).bin_code, (result).heap_types FROM wasm, compiled RETURNING name;
    ```

3. 访问 API：

    ```
    $ curl localhost:8080
    ```
