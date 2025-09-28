#include "postgres.h"
#include "getopt_long.h"
#include "common/jsonapi.h"
#include "mb/pg_wchar.h"
#include "utils/builtins.h"
#include "utils/json.h"

#include "rustica/env.h"
#include "rustica/moontest.h"

extern const char *progname;

struct option moontest_options[] = { { "spec", required_argument, NULL, 's' },
                                     { "help", no_argument, NULL, 'h' },
                                     { NULL, 0, NULL, 0 } };

int
moontest_parse_args(int argc,
                    char *argv[],
                    const char **moontest_spec,
                    const char **wasm_file) {
    int c;
    while ((c = getopt_long(argc, argv, "s:h", moontest_options, NULL)) != -1) {
        switch (c) {
            case 's':
                *moontest_spec = optarg;
                break;
            case 'h':
                printf(_("Usage:\n"));
                printf("  %s moontest --spec <JSON> <wasm_file>\n", progname);
                printf(_("Options:\n"));
                printf(_(
                    "  --spec <JSON>    JSON spec describing tests to run\n"));
                printf(_("  -h, --help       show this help, then exit\n"));
                return 0;
            default:
                fprintf(stderr, "Unknown option for 'moontest'\n");
                return 1;
        }
    }
    if (optind < argc)
        *wasm_file = argv[optind];
    if (!*moontest_spec) {
        fprintf(stderr, "Error: --spec <JSON> is required for 'moontest'\n");
        return 1;
    }
    if (!*wasm_file) {
        fprintf(stderr, "Error: No wasm file specified for 'moontest'\n");
        return 1;
    }
    return 0;
}

typedef enum Expect {
    E_TOP = 0,
    E_TOP_FIELD,
    E_PACKAGE_NAME,
    E_FILE_ARRAY,
    E_FILE_TUPLE,
    E_FILE_NAME,
    E_RANGE_ARRAY,
    E_RANGE_OBJECT,
    E_RANGE_FIELD,
    E_RANGE_START,
    E_RANGE_END,
    E_FILE_END,
    E_END,

} Expect;

typedef struct JsonParseState {
    Expect expect;

    wasm_exec_env_t exec_env;
    wasm_module_inst_t module_inst;
    wasm_function_inst_t exec;

    char *package;
    StringInfo package_buf;

    int current_file_index;
    char *filename;

    int current_range_index;
    int range_start;
    int range_end;
    bool range_start_set;
    bool range_end_set;
} JsonParseState;

static JsonParseErrorType
json_object_start_cb(void *state);
static JsonParseErrorType
json_object_end_cb(void *state);
static JsonParseErrorType
json_array_start_cb(void *state);
static JsonParseErrorType
json_array_end_cb(void *state);
static JsonParseErrorType
json_object_field_start_cb(void *state, char *fname, bool isnull);
static JsonParseErrorType
json_scalar_cb(void *state, char *token, JsonTokenType tokentype);

void
moontest_run(wasm_exec_env_t exec_env, const char *moontest_spec) {
    JsonLexContext *lex;
    JsonSemAction sem;
    JsonParseState parse_state;

    /* Initialize parsing state */
    memset(&parse_state, 0, sizeof(JsonParseState));
    parse_state.exec_env = exec_env;
    parse_state.module_inst = wasm_runtime_get_module_inst(exec_env);
    parse_state.package = "\"<unknown package>\"";
    parse_state.exec =
        wasm_runtime_lookup_function(parse_state.module_inst,
                                     "moonbit_test_driver_internal_execute");
    if (!parse_state.exec)
        ereport(ERROR,
                (errcode(ERRCODE_UNDEFINED_FUNCTION),
                 errmsg("Function 'moonbit_test_driver_internal_execute' "
                        "not found in module")));

    /* Set up JSON semantic actions */
    memset(&sem, 0, sizeof(JsonSemAction));
    sem.semstate = &parse_state;
    sem.object_start = json_object_start_cb;
    sem.object_end = json_object_end_cb;
    sem.array_start = json_array_start_cb;
    sem.array_end = json_array_end_cb;
    sem.object_field_start = json_object_field_start_cb;
    sem.scalar = json_scalar_cb;

    /* Create lexer context and parse JSON once */
    lex = makeJsonLexContextCstringLen(NULL,
                                       moontest_spec,
                                       strlen(moontest_spec),
                                       PG_UTF8,
                                       true);
    PG_TRY();
    {
        pg_parse_json(lex, &sem);
    }
    PG_FINALLY();
    {
        freeJsonLexContext(lex);
        if (parse_state.package_buf)
            destroyStringInfo(parse_state.package_buf);
        if (parse_state.filename)
            pfree(parse_state.filename);
    }
    PG_END_TRY();
}

static JsonParseErrorType
json_object_start_cb(void *state) {
    JsonParseState *parse_state = state;

    switch (parse_state->expect) {
        case E_TOP:
            parse_state->expect = E_TOP_FIELD;
            break;

        case E_RANGE_OBJECT:
            parse_state->expect = E_RANGE_FIELD;
            parse_state->range_start_set = false;
            parse_state->range_end_set = false;
            break;

        default:
            return JSON_SEM_ACTION_FAILED;
    }
    return JSON_SUCCESS;
}

static JsonParseErrorType
json_object_end_cb(void *state) {
    JsonParseState *parse_state = state;
    wasm_function_inst_t func;
    rustica_value_t filename = NULL;
    uintptr_t filename_ref = 0;
    StringInfo filename_buf = NULL, msg = NULL, escaped_msg = NULL;
    const char *exc;

    switch (parse_state->expect) {
        case E_RANGE_FIELD:
            if (!parse_state->range_start_set || !parse_state->range_end_set)
                ereport(
                    ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("Range object missing 'start' or 'end' field")));
            for (int i = parse_state->range_start; i < parse_state->range_end;
                 i++) {
                wasm_val_t args[2];
                if (!filename) {
                    Assert(parse_state->filename != NULL);
                    filename = rustica_value_new(RUSTICA_ENV_CSTRING,
                                                 parse_state->filename,
                                                 0);
                }
                if (!filename_ref)
                    filename_ref =
                        (uintptr_t)rustica_value_to_wasm(parse_state->exec_env,
                                                         filename);

                args[0].kind = WASM_EXTERNREF;
                args[0].of.foreign = filename_ref;
                args[1].kind = WASM_I32;
                args[1].of.i32 = i;

                if (!wasm_runtime_call_wasm_a(parse_state->exec_env,
                                              parse_state->exec,
                                              0,
                                              NULL,
                                              2,
                                              args)) {
                    if (!filename_buf) {
                        filename_buf = makeStringInfo();
                        escape_json(filename_buf, filename->ptr);
                    }
                    if (msg) {
                        resetStringInfo(msg);
                    }
                    else {
                        msg = makeStringInfo();
                        enlargeStringInfo(msg, 4096);
                    }
                    exc = wasm_runtime_get_exception(parse_state->module_inst);
                    appendStringInfoString(msg, exc ? exc : "Error: \n");
                    msg->len += (int)wasm_runtime_dump_call_stack_to_buf(
                        parse_state->exec_env,
                        msg->data + msg->len,
                        msg->maxlen - msg->len - 1);
                    while (msg->len >= 2 && msg->data[msg->len - 2] == '\n')
                        msg->data[msg->len-- - 2] = '\0';
                    if (escaped_msg)
                        resetStringInfo(escaped_msg);
                    else
                        escaped_msg = makeStringInfo();
                    escape_json(escaped_msg, msg->data);

                    printf("----- BEGIN MOON TEST RESULT -----\n");
                    printf("{\"package\": %s, \"filename\": %s, \"index\": "
                           "\"%d\", "
                           "\"test_name\": \"%d\", \"message\": %s}\n",
                           parse_state->package,
                           filename_buf->data,
                           i,
                           i,
                           escaped_msg->data);
                    printf("----- END MOON TEST RESULT -----\n");
                    wasm_runtime_set_exception(parse_state->module_inst, NULL);
                }
            }
            if (filename_buf)
                destroyStringInfo(filename_buf);
            if (msg)
                destroyStringInfo(msg);
            if (escaped_msg)
                destroyStringInfo(escaped_msg);
            parse_state->current_range_index += 1;
            parse_state->expect = E_RANGE_OBJECT;
            break;

        case E_TOP_FIELD:
            parse_state->expect = E_END;
            func = wasm_runtime_lookup_function(parse_state->module_inst,
                                                "moonbit_test_driver_finish");
            if (func)
                wasm_runtime_call_wasm(parse_state->exec_env, func, 0, NULL);
            break;

        default:
            return JSON_SEM_ACTION_FAILED;
    }

    return JSON_SUCCESS;
}

static JsonParseErrorType
json_array_start_cb(void *state) {
    JsonParseState *parse_state = state;

    switch (parse_state->expect) {
        case E_FILE_ARRAY:
            parse_state->expect = E_FILE_TUPLE;
            break;

        case E_FILE_TUPLE:
            parse_state->expect = E_FILE_NAME;
            break;

        case E_RANGE_ARRAY:
            parse_state->expect = E_RANGE_OBJECT;
            parse_state->current_range_index = 0;
            break;

        default:
            return JSON_SEM_ACTION_FAILED;
    }
    return JSON_SUCCESS;
}

static JsonParseErrorType
json_array_end_cb(void *state) {
    JsonParseState *parse_state = state;

    switch (parse_state->expect) {
        case E_RANGE_OBJECT:
            parse_state->expect = E_FILE_END;
            break;

        case E_FILE_END:
            pfree(parse_state->filename);
            parse_state->filename = NULL;
            parse_state->expect = E_FILE_TUPLE;
            break;

        default:
            return JSON_SEM_ACTION_FAILED;
    }

    return JSON_SUCCESS;
}

static JsonParseErrorType
json_object_field_start_cb(void *state, char *fname, bool isnull) {
    JsonParseState *parse_state = state;
    JsonParseErrorType rv = JSON_SUCCESS;

    PG_TRY();
    {
        switch (parse_state->expect) {
            case E_TOP_FIELD:
                if (strcmp(fname, "package") == 0) {
                    if (!isnull)
                        parse_state->expect = E_PACKAGE_NAME;
                }
                else if (strcmp(fname, "file_and_index") == 0) {
                    if (!isnull)
                        parse_state->expect = E_FILE_ARRAY;
                }
                else
                    ereport(ERROR,
                            (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                             errmsg("Unexpected field '%s' in top-level object",
                                    fname)));
                break;

            case E_RANGE_FIELD:
                if (strcmp(fname, "start") == 0) {
                    if (!isnull)
                        parse_state->expect = E_RANGE_START;
                }
                else if (strcmp(fname, "end") == 0) {
                    if (!isnull)
                        parse_state->expect = E_RANGE_END;
                }
                else
                    ereport(ERROR,
                            (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                             errmsg("Unexpected field '%s' in range object",
                                    fname)));
                break;

            default:
                rv = JSON_SEM_ACTION_FAILED;
        }
    }
    PG_FINALLY();
    {
        pfree(fname);
    }
    PG_END_TRY();
    return rv;
}

static JsonParseErrorType
json_scalar_cb(void *state, char *token, JsonTokenType tokentype) {
    JsonParseState *parse_state = state;
    JsonParseErrorType rv = JSON_SUCCESS;

    PG_TRY();
    {
        switch (parse_state->expect) {
            case E_PACKAGE_NAME:
                if (tokentype != JSON_TOKEN_STRING)
                    ereport(ERROR,
                            (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                             errmsg("Expected string for 'package' field")));
                parse_state->package_buf = makeStringInfo();
                escape_json(parse_state->package_buf, token);
                parse_state->package = parse_state->package_buf->data;
                parse_state->expect = E_TOP_FIELD;
                break;

            case E_FILE_NAME:
                if (tokentype != JSON_TOKEN_STRING)
                    ereport(ERROR,
                            (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                             errmsg("Expected string for filename in "
                                    "file_and_index entry")));
                parse_state->filename = token;
                token = NULL;
                parse_state->expect = E_RANGE_ARRAY;
                break;

            case E_RANGE_START:
                if (tokentype != JSON_TOKEN_NUMBER)
                    ereport(
                        ERROR,
                        (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                         errmsg(
                             "Expected number for 'start' in range object")));
                parse_state->range_start = pg_strtoint32(token);
                parse_state->range_start_set = true;
                parse_state->expect = E_RANGE_FIELD;
                break;

            case E_RANGE_END:
                if (tokentype != JSON_TOKEN_NUMBER)
                    ereport(
                        ERROR,
                        (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                         errmsg("Expected number for 'end' in range object")));
                parse_state->range_end = pg_strtoint32(token);
                parse_state->range_end_set = true;
                parse_state->expect = E_RANGE_FIELD;
                break;

            default:
                rv = JSON_SEM_ACTION_FAILED;
        }
    }
    PG_FINALLY();
    {
        if (token)
            pfree(token);
    }
    PG_END_TRY();
    return rv;
}
