# <img src="docs/logo.svg" height="48px" align="right">  燕几图引擎——变 PG 为 API 服务器！

[![English](https://img.shields.io/badge/英文-English-informational?logo=data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABsAAAAQCAYAAADnEwSWAAAABGdBTUEAALGPC/xhBQAAADhlWElmTU0AKgAAAAgAAYdpAAQAAAABAAAAGgAAAAAAAqACAAQAAAABAAAAG6ADAAQAAAABAAAAEAAAAACiF0fSAAABJUlEQVQ4EWP8//8/MwMDAysQEw0YGRl/EK0YWSHQsgogJgX8RNZPCpuJFMWUqqWrZSw4XBsCFL+FQ+4/DnEUYWC8gNKCEDB+X8MlgIKVWCJMD64ADwOorwmIHyNhdyDbFYgPAfFXIAaBV0CcCTIG5DNsLo0GKnDEYc8+oGsvQ+UEgLQMkrpYIDsSiJGjRxTInwY07wmuYCxDMgCdmQUUgFmGLheNLoDEzwG5gBFJgFQmNr3ZQEPsgfgImmEquHy2BajwHZpiGPcmjAGk0aPgCDCIp4HkgcHWA6RsQGwoEMEVZ9VATZdgqkig7yKp/YjEBjORIxJdjhw+3tIFVzCGAoPBEo9ta4E+f4NHHqsULstqsKpGCJ4BMkm2jNrBiHAOFhZdLQMA8pKhkQYZiokAAAAASUVORK5CYII=)](README.md)
[![许可](https://img.shields.io/badge/许可-木兰PSLv2-success?logo=opensourceinitiative&logoColor=white)](https://license.coscl.org.cn/MulanPSL2)
[![许可](https://img.shields.io/badge/许可-Apache--2.0-success?logo=opensourceinitiative&logoColor=white)](https://www.apache.org/licenses/LICENSE-2.0)


燕几图引擎是一个 PostgreSQL 的扩展程序，使用 WebAssembly
技术，在数据库中直接安全地运行业务逻辑 ，将数据库变成一个 API
服务器。

这个项目还在开发中，请勿用于生产环境。


## 特性

* **快速**：使用 WAMR/LLVM，预编译 WASM 和查询语句
* **安全**：多进程模型，严格的隔离设计
* **原生**：镜像 PostgreSQL 的数据和类型
* **零拷贝**：直接访问网络缓冲区和数据库共享内存

燕几图引擎没有使用 WASI 或是 Component Model，而是在更细的粒度上定义了一套
FFI 接口，为各种 PostgreSQL 的数据类型和函数提供了更优化的封装。

目前，以下编程语言可以生成适用于燕几图引擎的 WebAssembly 模块：

* 燕几图编程语言（使用 MoonGRES 后端）
* 月兔编程语言（通过 MoonGRES 扩展）

燕几图引擎还有一种独立的命令行形态，静态链接了 PostgreSQL
的后端代码，可以在不启动数据库的情况下，作为运行时来执行相同的
WebAssembly 程序，这对于调试和测试非常有用。


## 开发

此项目由改装过的 Meson 配置，Ninja 构建，而 Meson 本身的改装由 uv
来跑。`Makefile` 只提供一些便捷的命令封装。

* 安装系统依赖 (以 Arch Linux 为例)：

    ```
    $ sudo pacman -S make gcc ninja uv llvm18 bison flex
    ```

* 构建扩展程序并运行开发版的 Postgres：

    ```
    $ make
    $ make run
    ```


## 测试

1. 安装 MoonGRES：

    ```
    $ curl ...TBD... | sh
    ```

2. 用之前构建的 `rustica-engine` 替代 MoonGRES 自带的 `rustica-engine`：

    ```
    $ ln -sf $(pwd)/install/bin/rustica-engine ~/.moon/bin/rustica-engine
    ```

3. 运行 MoonBit core 测试套件：

    ```
    $ cd ~/.moon/lib/core && moon test --target moongres
    ```


## 使用许可

任何人都可以选择以下许可证之一，遵守条款并自由使用本项目：

* Apache 许可证，2.0 版
* 木兰宽松许可证，第 2 版

`SPDX-License-Identifier: Apache-2.0 OR MulanPSL-2.0`
