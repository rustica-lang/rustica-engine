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
    postgres=# DELETE FROM rustica.modules;
    postgres=# INSERT INTO rustica.modules SELECT 'main', code, rustica.compile_wasm(code) FROM (SELECT '\x0061736d01000000011f065e78015e630001600364007f7f017f60037f7f7f0164006000006000017f020c0103656e760473656e64000203060503020405040401000503010001060a016401004101fb07010b0707010372756e00030801050901000c01010a5a05300201630001640023002000fb0b012203d14504402003d40f0b20012002fb0900002104230020002004fb0e0120040f0b0a0020002001200210000b050010041a0b130041004100413310014100413310021a41000b02000b0b37010134485454502f312e3020323030204f4b0d0a436f6e74656e742d4c656e6774683a2031320d0a0d0a68656c6c6f20776f726c640a20'::bytea AS code) _;
    ```

3. Invoke the API:

    ```
    $ curl localhost:8080
    ```
