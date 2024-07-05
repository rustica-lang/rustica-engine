CREATE SCHEMA rustica;

CREATE FUNCTION rustica.compile_wasm(bytea) RETURNS bytea
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT;

CREATE TABLE rustica.modules(
    name text PRIMARY KEY,
    byte_code bytea NOT NULL,
    bin_code bytea NOT NULL
);

CREATE OR REPLACE FUNCTION rustica.invalidate_module_cache() RETURNS TRIGGER AS $$
    BEGIN
        IF TG_OP = 'DELETE' THEN
            PERFORM pg_notify('rustica_module_cache_invalidation', OLD.name);
        ELSE
            PERFORM pg_notify('rustica_module_cache_invalidation', NEW.name);
        END IF;
        RETURN NULL;
    END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER module_change
    AFTER INSERT OR UPDATE OR DELETE ON rustica.modules
    FOR EACH ROW EXECUTE FUNCTION rustica.invalidate_module_cache();
