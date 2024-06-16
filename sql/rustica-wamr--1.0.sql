CREATE SCHEMA rustica;

CREATE FUNCTION rustica.compile_wasm(bytea) RETURNS bytea
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT;

CREATE TABLE rustica.application(
    byte_code bytea NOT NULL,
    bin_code bytea NOT NULL
);
