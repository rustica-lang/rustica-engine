CREATE SCHEMA rustica;

CREATE FUNCTION rustica.compile_wasm(bytea) RETURNS bytea
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT;

CREATE TABLE rustica.modules(
    name text PRIMARY KEY,
    byte_code bytea NOT NULL,
    bin_code bytea NOT NULL
);
