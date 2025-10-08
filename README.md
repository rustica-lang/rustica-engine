# Rustica Engine

Rustica Engine 是一个将 WebAssembly 运行时 (WAMR) 与 PostgreSQL 后端集成的工具，允许在 PostgreSQL 环境中执行 WebAssembly 模块。

## 功能特性

- 基于 WAMR (WebAssembly Micro Runtime) 2.4.2
- 集成 PostgreSQL 17.6 后端
- 支持 WebAssembly GC (垃圾回收)
- 支持自定义原生函数绑定
- 支持调试功能（调用栈转储、自定义名称段等）

## 系统要求

### 依赖项

#### 构建工具
- **Meson** >= 1.9.0
- **Ninja** (构建后端)
- **uv** (Python 包管理器)
- **CMake** (用于构建 WAMR)

#### PostgreSQL 依赖
- **开发库和头文件**：
  - `libreadline-dev` (命令行编辑)
  - `zlib1g-dev` (压缩支持)
  - `libssl-dev` (SSL/TLS 支持)
  - `libxml2-dev` (XML 支持，可选)
  - `libxslt1-dev` (XSLT 支持，可选)
  - `liblz4-dev` (LZ4 压缩，可选)
  - `libzstd-dev` (Zstandard 压缩，可选)
  - `libpam0g-dev` (PAM 认证，可选)
  - `libldap2-dev` (LDAP 支持，可选)
  - `libsystemd-dev` (systemd 集成，可选)
  - `libicu-dev` (国际化支持)
  - `libedit-dev` (命令行编辑，readline 的替代)

#### UUID 支持
- **e2fsprogs** (`uuid-dev` 或 `e2fsprogs-devel`)
  - 项目配置为使用 e2fs 的 UUID 实现而非 ossp-uuid

#### 编译器
- GCC 或 Clang (支持 C11 标准)

### 在 Ubuntu/Debian 上安装依赖

```bash
# 安装基础构建工具
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build

# 安装 PostgreSQL 依赖
sudo apt-get install -y \
    libreadline-dev \
    zlib1g-dev \
    libssl-dev \
    libicu-dev \
    libedit-dev \
    uuid-dev

# 安装可选的 PostgreSQL 依赖
sudo apt-get install -y \
    libxml2-dev \
    libxslt1-dev \
    liblz4-dev \
    libzstd-dev \
    libpam0g-dev \
    libldap2-dev \
    libsystemd-dev

# 安装 uv
curl -LsSf https://astral.sh/uv/install.sh | sh
```

### 在 Arch Linux 上安装依赖

```bash
# 安装基础构建工具
sudo pacman -S base-devel cmake ninja

# 安装 PostgreSQL 依赖
sudo pacman -S \
    readline \
    zlib \
    openssl \
    icu \
    libedit \
    e2fsprogs

# 安装可选的 PostgreSQL 依赖
sudo pacman -S \
    libxml2 \
    libxslt \
    lz4 \
    zstd \
    pam \
    libldap \
    systemd-libs

# 安装 Python 和 uv
curl -LsSf https://astral.sh/uv/install.sh | sh
```

## 构建

1. 克隆仓库：
```bash
git clone https://github.com/yourusername/rustica-engine.git
cd rustica-engine
```

2. 使用 uv 设置 Python 环境：
```bash
uv sync
```

3. 配置构建：
```bash
uv run meson.py setup build
```

4. 编译 rustica-engine：
```bash
ninja -C build rustica-engine
```

## 使用方法

运行 WebAssembly 文件：
```bash
./build/rustica-engine run <wasm_file_path>
```

查看帮助信息：
```bash
./build/rustica-engine --help
```

查看版本信息：
```bash
./build/rustica-engine --version
```

## 示例

```bash
# 运行一个 WebAssembly 模块
./build/rustica-engine run example.wasm

# 查看版本
./build/rustica-engine -V
# 输出: rustica-engine (PostgreSQL 17.6, WAMR 2.4.2)
```

## 项目结构

- `main.c` - 主程序入口，包含 WAMR 运行时初始化和 WebAssembly 模块加载逻辑
- `meson.build` - Meson 构建配置文件
- `meson.py` - 扩展 Meson 功能，如获取 cmake 的变量
- `pyproject.toml` - Python 项目配置
- `subprojects/` - 子项目依赖（PostgreSQL 和 WAMR）

## 技术细节

### WebAssembly 支持
- 使用 WAMR 2.4.2 作为 WebAssembly 运行时
- 启用了 GC（垃圾回收）支持
- 支持扩展常量表达式
- 支持自定义段加载
- 支持调用栈转储用于调试

### PostgreSQL 集成
- 基于 PostgreSQL 17.6
- 使用 PostgreSQL 的内存管理和错误处理机制
- 支持 PostgreSQL 的本地化功能
