MODULE_big = rustica-wamr
OBJS = src/master.o

EXTENSION = rustica-wamr
DATA = sql/rustica-wamr--1.0.sql

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)

include $(PGXS)
