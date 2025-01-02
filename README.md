## Production

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
    postgres=# CREATE EXTENSION "rustica-wamr" CASCADE;
    postgres=# DELETE FROM rustica.queries;
    postgres=# DELETE FROM rustica.modules;
    postgres=# WITH wasm AS (SELECT '\x0061736d01000000010f035e7801600364007f7f017f600000020c0103656e760473656e6400010304030102020401000503010001060100070a01065f737461727400030801020901000c01010a21030a0020002001200210000b02000b110041004133fb0900004100413310011a0b0b36010133485454502f312e3020323030204f4b0d0a436f6e74656e742d4c656e6774683a2031320d0a0d0a68656c6c6f20776f726c640a'::bytea AS code), compiled AS (SELECT rustica.compile_wasm(code) AS result FROM wasm) INSERT INTO rustica.modules SELECT 'main', code, (result).bin_code, (result).heap_types FROM wasm, compiled RETURNING name;
    ```

3. Invoke the API:

    ```
    $ curl localhost:8080
    ```
