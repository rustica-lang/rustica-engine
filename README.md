## Production

1. Install system dependencies

    ```
    $ sudo pacman -S llvm postgresql
    ```

2. Build and install

    ```
    $ make
    $ sudo make install
    ```

## Development

### Initialization

1. Install system dependencies

    ```
    $ sudo pacman -S llvm lldb
    ```

2. Initialize vendor libraries and PostgreSQL for development:

    ```
    $ make dev-pg
    ```

### Iteration

* Build extension and reload the dev Postgres:

    ```
    $ make reload DEV=1 -j $(nproc)
    ```

    You can join this command with tailing the log:

    ```
    $ make reload DEV=1 -j $(nproc) && tail -f vendor/pg*.log
    ```

* When you changed settings in Makefile, rebuild extension files:

    ```
    $ make clean DEV=1
    ```

* When you need to nuke the environment:

    ```
    $ make clean-vendor
    ```

### Testing

1. Convert WASM binary to octets for step 2:

    ```
    $ xxd -p target/wasm-gc/release/build/main/main.wasm | tr -d '\n'
    ```

2. Deploy the WASM application:

    ```
    $ psql -h /tmp postgres
    postgres=# CREATE EXTENSION "rustica-wamr" CASCADE;
    postgres=# DELETE FROM rustica.modules;
    postgres=# INSERT INTO rustica.modules SELECT 'main', code, rustica.compile_wasm(code) FROM (SELECT '\x0061736d01000000010401600000020100030201000401000503010001060100070a01065f737461727400000901000c01000a040102000b0b0100'::bytea AS code) _;
    ```

3. Invoke the API:

    ```
    $ curl localhost:8080
    ```
