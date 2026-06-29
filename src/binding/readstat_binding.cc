// binding/readstat_binding.cc
#include <napi.h>
#include <vector>
#include <map>
#include <string>
#include <memory>
#include <set>
#include <algorithm>
#include <cstring>
#include "./ReadStat/src/readstat.h"
#include <ctime>
#include <iomanip>
#include <sstream>

// Helper function to convert readstat_compress_t to a string
const char* compressionTypeToString(readstat_compress_t compression) {
    switch(compression) {
        case READSTAT_COMPRESS_NONE:
            return "NONE";
        case READSTAT_COMPRESS_ROWS:
            return "ROWS";
        case READSTAT_COMPRESS_BINARY:
            return "BINARY";
        default:
            return "UNKNOWN";
    }
}

// Context structure to pass data between callbacks
struct context_t {
    int var_count;     // Store the total number of variables
    int current_var;   // Track the current variable in each row
    int current_record; // Current record being processed

    // Storage for variable metadata
    std::vector<std::string> var_names;
    std::vector<readstat_type_t> var_types;

    // Storage for data rows - changed from map to vector for array-based output
    std::vector<std::vector<Napi::Value>> rows;
    Napi::Env env;

    // Constructor to fix the initialization issue
    context_t(Napi::Env e) :
        var_count(0),
        current_var(0),
        current_record(0),
        env(e) {}
};

// Enhanced metadata context structure
struct metadata_context_t {
    readstat_metadata_t *metadata;
    std::vector<readstat_variable_t*> variables;
    Napi::Env env;
    Napi::Object dataset;      // Store the result directly in the context
    Napi::Array columns;       // Store columns array directly

    // Constructor with enhanced initialization
    metadata_context_t(Napi::Env e) :
        metadata(nullptr),
        env(e),
        dataset(Napi::Object::New(e)),
        columns(Napi::Array::New(e)) {}
};

// --- START ASYNC SUPPORT ---

enum ObsType { OBS_STRING, OBS_DOUBLE, OBS_INT, OBS_NULL };

struct ObsValue {
    ObsType type;
    std::string str_val;
    double dbl_val;
    int32_t int_val;

    ObsValue() : type(OBS_NULL), dbl_val(0), int_val(0) {}
};

static ObsValue makeObsValue(readstat_variable_t *variable, readstat_value_t value) {
    ObsValue obs;

    if (readstat_value_is_missing(value, variable)) {
        return obs;
    }

    switch(readstat_value_type(value)) {
        case READSTAT_TYPE_STRING:
            obs.type = OBS_STRING;
            obs.str_val = readstat_string_value(value);
            break;
        case READSTAT_TYPE_INT8:
        case READSTAT_TYPE_INT16:
        case READSTAT_TYPE_INT32:
            obs.type = OBS_INT;
            obs.int_val = readstat_int32_value(value);
            break;
        case READSTAT_TYPE_FLOAT:
        case READSTAT_TYPE_DOUBLE:
            obs.type = OBS_DOUBLE;
            obs.dbl_val = readstat_double_value(value);
            break;
        default:
            obs.type = OBS_NULL;
    }

    return obs;
}

static Napi::Value convertObsValue(Napi::Env env, const ObsValue& obs) {
    switch(obs.type) {
        case OBS_STRING:
            return Napi::String::New(env, obs.str_val);
        case OBS_INT:
            return Napi::Number::New(env, obs.int_val);
        case OBS_DOUBLE:
            return Napi::Number::New(env, obs.dbl_val);
        default:
            return env.Null();
    }
}

typedef readstat_error_t (*readstat_parse_fn_t)(readstat_parser_t *parser, const char *path, void *user_ctx);

struct format_binding_config_t {
    const char *file_format;
    const char *source_system;
    readstat_parse_fn_t parse;
};

static const format_binding_config_t SAS7BDAT_FORMAT = {
    "SAS7BDAT",
    "SAS",
    &readstat_parse_sas7bdat,
};

static const format_binding_config_t DTA_FORMAT = {
    "DTA",
    "Stata",
    &readstat_parse_dta,
};

static const format_binding_config_t SAV_FORMAT = {
    "SAV",
    "SPSS",
    &readstat_parse_sav,
};

static const format_binding_config_t ZSAV_FORMAT = {
    "ZSAV",
    "SPSS",
    &readstat_parse_sav,
};

static const format_binding_config_t POR_FORMAT = {
    "POR",
    "SPSS",
    &readstat_parse_por,
};

static Napi::Value ReadFormat(
    const Napi::CallbackInfo& info,
    const format_binding_config_t& formatConfig
);

struct row_count_context_t {
    long row_count;
    long last_obs_index;

    row_count_context_t() : row_count(0), last_obs_index(-1) {}
};

static int count_rows_handle_value(
    int obs_index,
    readstat_variable_t *variable,
    readstat_value_t value,
    void *ctx
) {
    row_count_context_t *context = (row_count_context_t *)ctx;
    (void)variable;
    (void)value;

    if (context->last_obs_index != obs_index) {
        context->row_count++;
        context->last_obs_index = obs_index;
    }

    return READSTAT_HANDLER_OK;
}

static readstat_error_t count_format_rows(
    const std::string& filePath,
    const format_binding_config_t& formatConfig,
    long *rowCount
) {
    row_count_context_t context;
    readstat_parser_t *parser = readstat_parser_init();
    readstat_set_value_handler(parser, &count_rows_handle_value);

    readstat_error_t error = formatConfig.parse(parser, filePath.c_str(), &context);
    readstat_parser_free(parser);

    if (error == READSTAT_OK) {
        *rowCount = context.row_count;
    }

    return error;
}

static std::vector<std::string> parseStringArray(const Napi::Value& value, const char *fieldName) {
    std::vector<std::string> result;
    if (value.IsUndefined() || value.IsNull()) {
        return result;
    }
    if (!value.IsArray()) {
        throw Napi::TypeError::New(value.Env(), std::string(fieldName) + " must be an array of strings");
    }

    Napi::Array values = value.As<Napi::Array>();
    result.reserve(values.Length());
    for (uint32_t index = 0; index < values.Length(); index++) {
        Napi::Value entry = values.Get(index);
        if (!entry.IsString()) {
            throw Napi::TypeError::New(value.Env(), std::string(fieldName) + " must be an array of strings");
        }
        result.push_back(entry.As<Napi::String>().Utf8Value());
    }

    return result;
}

static std::vector<long> parseLongArray(const Napi::Value& value, const char *fieldName) {
    std::vector<long> result;
    if (value.IsUndefined() || value.IsNull()) {
        return result;
    }
    if (!value.IsArray()) {
        throw Napi::TypeError::New(value.Env(), std::string(fieldName) + " must be an array of integers");
    }

    Napi::Array values = value.As<Napi::Array>();
    result.reserve(values.Length());
    for (uint32_t index = 0; index < values.Length(); index++) {
        Napi::Value entry = values.Get(index);
        if (!entry.IsNumber()) {
            throw Napi::TypeError::New(value.Env(), std::string(fieldName) + " must be an array of integers");
        }

        long rowNumber = entry.As<Napi::Number>().Int64Value();
        if (rowNumber < 0) {
            throw Napi::RangeError::New(value.Env(), std::string(fieldName) + " values must be non-negative");
        }
        result.push_back(rowNumber);
    }

    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());

    return result;
}

struct async_context_t {
    int var_count;
    int current_var;
    std::vector<std::vector<ObsValue>> rows;
    std::string error_message; // To capture errors during parsing in background

    async_context_t() : var_count(0), current_var(0) {}
};

static int async_handle_metadata(readstat_metadata_t *metadata, void *ctx) {
    async_context_t *context = (async_context_t *)ctx;
    context->var_count = readstat_get_var_count(metadata);
    context->current_var = 0;
    return READSTAT_HANDLER_OK;
}

static int async_handle_variable(int index, readstat_variable_t *variable, const char *val_labels, void *ctx) {
    return READSTAT_HANDLER_OK;
}

static int async_handle_value(int obs_index, readstat_variable_t *variable, readstat_value_t value, void *ctx) {
    async_context_t *context = (async_context_t *)ctx;

    int var_idx = readstat_variable_get_index(variable);
    context->current_var = var_idx;

    if (var_idx == 0) {
        context->rows.push_back(std::vector<ObsValue>(context->var_count));
    }

    context->rows.back()[var_idx] = makeObsValue(variable, value);

    return READSTAT_HANDLER_OK;
}

struct stream_emit_result_t {
    bool ok;
    bool stop_requested;
    long last_consumed_row;
    std::string error_message;
};

class ReadStatStreamWorker;

struct stream_context_t {
    int var_count;
    size_t chunk_size;
    long start_row;
    long rows_seen;
    long last_row;
    long chunk_start_row;
    bool stop_requested;
    ReadStatStreamWorker *worker;
    std::vector<std::vector<ObsValue>> chunk_rows;
    std::string error_message;

    stream_context_t(size_t stream_chunk_size, long row_start, ReadStatStreamWorker *stream_worker) :
        var_count(0),
        chunk_size(stream_chunk_size),
        start_row(row_start),
        rows_seen(0),
        last_row(row_start - 1),
        chunk_start_row(row_start),
        stop_requested(false),
        worker(stream_worker) {}
};

    static int stream_handle_metadata(readstat_metadata_t *metadata, void *ctx);
    static int stream_handle_variable(int index, readstat_variable_t *variable, const char *val_labels, void *ctx);
    static int stream_handle_value(int obs_index, readstat_variable_t *variable, readstat_value_t value, void *ctx);

    static bool parse_stream_callback_result(
        napi_env env,
        napi_value callbackValue,
        bool *stopRequested,
        size_t *rowsUsed,
        std::string *errorMessage
    ) {
        napi_valuetype callbackType = napi_undefined;
        if (napi_typeof(env, callbackValue, &callbackType) != napi_ok) {
            *errorMessage = "Failed to inspect stream callback return value";
            return false;
        }

        if (callbackType == napi_boolean) {
            bool parsedStopRequested = false;
            if (napi_get_value_bool(env, callbackValue, &parsedStopRequested) == napi_ok) {
                *stopRequested = parsedStopRequested;
            }
            return true;
        }

        if (callbackType == napi_number) {
            double parsedRowsUsed = 0;
            if (napi_get_value_double(env, callbackValue, &parsedRowsUsed) == napi_ok) {
                *stopRequested = true;
                *rowsUsed = parsedRowsUsed > 0 ? static_cast<size_t>(parsedRowsUsed) : 0;
            }
            return true;
        }

        if (callbackType == napi_object) {
            bool hasStop = false;
            if (napi_has_named_property(env, callbackValue, "stop", &hasStop) == napi_ok && hasStop) {
                napi_value stopValue = nullptr;
                if (napi_get_named_property(env, callbackValue, "stop", &stopValue) == napi_ok) {
                    bool parsedStopRequested = false;
                    if (napi_get_value_bool(env, stopValue, &parsedStopRequested) == napi_ok) {
                        *stopRequested = parsedStopRequested;
                    }
                }
            }

            bool hasRowsUsed = false;
            if (napi_has_named_property(env, callbackValue, "rowsUsed", &hasRowsUsed) == napi_ok && hasRowsUsed) {
                napi_value rowsUsedValue = nullptr;
                if (napi_get_named_property(env, callbackValue, "rowsUsed", &rowsUsedValue) == napi_ok) {
                    double parsedRowsUsed = 0;
                    if (napi_get_value_double(env, rowsUsedValue, &parsedRowsUsed) == napi_ok) {
                        *rowsUsed = parsedRowsUsed > 0 ? static_cast<size_t>(parsedRowsUsed) : 0;
                        *stopRequested = true;
                    }
                }
            }
        }

        return true;
    }

    struct sync_stream_context_t {
        Napi::Env env;
        Napi::FunctionReference callback;
        int var_count;
        int selected_var_count;
        size_t chunk_size;
        long start_row;
        long rows_seen;
        long last_row;
        long chunk_start_row;
        bool stop_requested;
        std::set<std::string> selected_columns;
        std::vector<std::vector<ObsValue>> chunk_rows;
        std::string error_message;

        sync_stream_context_t(
            Napi::Env environment,
            const Napi::Function& callbackFunction,
            size_t stream_chunk_size,
            long row_start,
            const std::vector<std::string>& projectedColumns
        ) :
            env(environment),
            callback(Napi::Persistent(callbackFunction)),
            var_count(0),
            selected_var_count(0),
            chunk_size(stream_chunk_size),
            start_row(row_start),
            rows_seen(0),
            last_row(row_start - 1),
            chunk_start_row(row_start),
            stop_requested(false),
            selected_columns(projectedColumns.begin(), projectedColumns.end()) {}
    };

    struct selected_read_context_t {
        int var_count;
        int selected_var_count;
        long start_row;
        long active_selected_obs_index;
        std::set<std::string> selected_columns;
        std::vector<long> selected_rows;
        size_t next_selected_row_index;
        std::vector<std::vector<ObsValue>> rows;

        selected_read_context_t(
            long row_start,
            const std::vector<std::string>& projectedColumns,
            const std::vector<long>& projectedRows
        ) :
            var_count(0),
            selected_var_count(0),
            start_row(row_start),
                active_selected_obs_index(-1),
            selected_columns(projectedColumns.begin(), projectedColumns.end()),
            selected_rows(projectedRows),
            next_selected_row_index(0) {}
    };

    static bool emit_sync_stream_chunk(sync_stream_context_t *context) {
        if (context->chunk_rows.empty()) {
            return true;
        }

        Napi::Array jsRows = Napi::Array::New(context->env, context->chunk_rows.size());
        for (size_t rowIndex = 0; rowIndex < context->chunk_rows.size(); rowIndex++) {
            Napi::Array row = Napi::Array::New(context->env, context->chunk_rows[rowIndex].size());
            for (size_t columnIndex = 0; columnIndex < context->chunk_rows[rowIndex].size(); columnIndex++) {
                row[columnIndex] = convertObsValue(context->env, context->chunk_rows[rowIndex][columnIndex]);
            }
            jsRows[rowIndex] = row;
        }

        napi_value callbackArgs[2] = {
            jsRows,
            Napi::Number::New(context->env, context->chunk_start_row),
        };
        napi_value callbackValue = nullptr;

        napi_status callbackStatus = napi_call_function(
            context->env,
            context->env.Global(),
            context->callback.Value(),
            2,
            callbackArgs,
            &callbackValue
        );

        if (callbackStatus != napi_ok || context->env.IsExceptionPending()) {
            context->error_message = context->env.GetAndClearPendingException().Message();
            return false;
        }

        bool stopRequested = false;
        size_t rowsUsed = context->chunk_rows.size();
        if (!parse_stream_callback_result(context->env, callbackValue, &stopRequested, &rowsUsed, &context->error_message)) {
            return false;
        }

        if (rowsUsed > context->chunk_rows.size()) {
            rowsUsed = context->chunk_rows.size();
        }

        context->stop_requested = stopRequested;
        if (rowsUsed == 0) {
            context->last_row = context->chunk_start_row - 1;
        } else {
            context->last_row = context->chunk_start_row + static_cast<long>(rowsUsed) - 1;
        }

        context->chunk_rows.clear();
        return true;
    }

    static int sync_stream_handle_metadata(readstat_metadata_t *metadata, void *ctx) {
        sync_stream_context_t *context = (sync_stream_context_t *)ctx;
        context->var_count = readstat_get_var_count(metadata);
        context->selected_var_count = 0;
        return READSTAT_HANDLER_OK;
    }

    static int sync_stream_handle_variable(int index, readstat_variable_t *variable, const char *val_labels, void *ctx) {
        sync_stream_context_t *context = (sync_stream_context_t *)ctx;
        if (!context->selected_columns.empty()) {
            const std::string variableName = readstat_variable_get_name(variable);
            if (context->selected_columns.find(variableName) == context->selected_columns.end()) {
                return READSTAT_HANDLER_SKIP_VARIABLE;
            }
        }

        context->selected_var_count++;
        return READSTAT_HANDLER_OK;
    }

    static int sync_stream_handle_value(int obs_index, readstat_variable_t *variable, readstat_value_t value, void *ctx) {
        sync_stream_context_t *context = (sync_stream_context_t *)ctx;
        int var_idx = readstat_variable_get_index_after_skipping(variable);

        if (var_idx == 0) {
            if (context->chunk_rows.empty()) {
                context->chunk_start_row = context->start_row + context->rows_seen;
            }
            context->chunk_rows.push_back(std::vector<ObsValue>(context->selected_var_count));
        }

        context->chunk_rows.back()[var_idx] = makeObsValue(variable, value);

        if (var_idx == context->selected_var_count - 1) {
            context->rows_seen++;
            context->last_row = context->start_row + context->rows_seen - 1;

            if (context->chunk_rows.size() >= context->chunk_size) {
                if (!emit_sync_stream_chunk(context)) {
                    return READSTAT_HANDLER_ABORT;
                }

                if (context->stop_requested) {
                    return READSTAT_HANDLER_ABORT;
                }
            }
        }

        return READSTAT_HANDLER_OK;
    }

static int selected_read_handle_metadata(readstat_metadata_t *metadata, void *ctx) {
    selected_read_context_t *context = (selected_read_context_t *)ctx;
    context->var_count = readstat_get_var_count(metadata);
    context->selected_var_count = 0;
    return READSTAT_HANDLER_OK;
}

static int selected_read_handle_variable(int index, readstat_variable_t *variable, const char *val_labels, void *ctx) {
    selected_read_context_t *context = (selected_read_context_t *)ctx;
    if (!context->selected_columns.empty()) {
        const std::string variableName = readstat_variable_get_name(variable);
        if (context->selected_columns.find(variableName) == context->selected_columns.end()) {
            return READSTAT_HANDLER_SKIP_VARIABLE;
        }
    }

    context->selected_var_count++;
    return READSTAT_HANDLER_OK;
}

static int selected_read_handle_row(long obs_index, void *ctx) {
    selected_read_context_t *context = (selected_read_context_t *)ctx;
    if (context->selected_rows.empty()) {
        return READSTAT_HANDLER_OK;
    }

    long absoluteRow = context->start_row + obs_index;
    while (
        context->next_selected_row_index < context->selected_rows.size() &&
        context->selected_rows[context->next_selected_row_index] < absoluteRow
    ) {
        context->next_selected_row_index++;
    }

    if (context->next_selected_row_index >= context->selected_rows.size()) {
        return READSTAT_HANDLER_ABORT;
    }

    if (context->selected_rows[context->next_selected_row_index] > absoluteRow) {
        return READSTAT_HANDLER_SKIP_ROW;
    }

    context->active_selected_obs_index = obs_index;
    context->next_selected_row_index++;
    return READSTAT_HANDLER_OK;
}

static int selected_read_handle_value(int obs_index, readstat_variable_t *variable, readstat_value_t value, void *ctx) {
    selected_read_context_t *context = (selected_read_context_t *)ctx;
    int var_idx = readstat_variable_get_index_after_skipping(variable);

    if (!context->selected_rows.empty() && context->active_selected_obs_index != obs_index) {
        long absoluteRow = context->start_row + obs_index;
        while (
            context->next_selected_row_index < context->selected_rows.size() &&
            context->selected_rows[context->next_selected_row_index] < absoluteRow
        ) {
            context->next_selected_row_index++;
        }

        if (context->next_selected_row_index >= context->selected_rows.size()) {
            return READSTAT_HANDLER_ABORT;
        }

        if (context->selected_rows[context->next_selected_row_index] > absoluteRow) {
            return READSTAT_HANDLER_OK;
        }

        context->active_selected_obs_index = obs_index;
        context->next_selected_row_index++;
    }

    if (var_idx == 0) {
        context->rows.push_back(std::vector<ObsValue>(context->selected_var_count));
    }

    context->rows.back()[var_idx] = makeObsValue(variable, value);

    if (var_idx == context->selected_var_count - 1) {
        context->active_selected_obs_index = -1;
    }

    return READSTAT_HANDLER_OK;
}

class ReadStatStreamWorker : public Napi::AsyncWorker {
public:
    ReadStatStreamWorker(Napi::Env env, const std::string& filePath, long offset, size_t chunkSize,
            Napi::Function callback)
        : Napi::AsyncWorker(env),
          filePath(filePath),
          offset(offset),
          deferred(Napi::Promise::Deferred::New(env)),
          tsfn(Napi::ThreadSafeFunction::New(env, callback, "readSas7bdatStream", 0, 1)),
          context(chunkSize, offset, this),
          parserReachedEnd(false),
          tsfnReleased(false) {}

    Napi::Promise GetPromise() { return deferred.Promise(); }

    stream_emit_result_t EmitChunk(std::vector<std::vector<ObsValue>>&& rows, long chunkStartRow) {
        stream_emit_result_t result = {
            true,
            false,
            chunkStartRow + static_cast<long>(rows.size()) - 1,
            "",
        };

        if (rows.empty()) {
            result.last_consumed_row = chunkStartRow - 1;
            return result;
        }

        struct callback_result_t {
            bool stop_requested = false;
            size_t rows_used = 0;
            std::string error_message;
        };

        const auto callbackResult = std::make_shared<callback_result_t>();
        const auto chunkRows = std::make_shared<std::vector<std::vector<ObsValue>>>(std::move(rows));
        callbackResult->rows_used = chunkRows->size();

        napi_status status = tsfn.BlockingCall([chunkRows, chunkStartRow, callbackResult](Napi::Env env, Napi::Function jsCallback) {
            Napi::Array jsRows = Napi::Array::New(env, chunkRows->size());

            for (size_t rowIndex = 0; rowIndex < chunkRows->size(); rowIndex++) {
                Napi::Array row = Napi::Array::New(env, (*chunkRows)[rowIndex].size());
                for (size_t columnIndex = 0; columnIndex < (*chunkRows)[rowIndex].size(); columnIndex++) {
                    row[columnIndex] = convertObsValue(env, (*chunkRows)[rowIndex][columnIndex]);
                }
                jsRows[rowIndex] = row;
            }

            napi_value callbackArgs[2] = {
                jsRows,
                Napi::Number::New(env, chunkStartRow),
            };
            napi_value callbackValue = nullptr;

            napi_status callbackStatus = napi_call_function(
                env,
                env.Global(),
                jsCallback,
                2,
                callbackArgs,
                &callbackValue
            );

            if (callbackStatus != napi_ok || env.IsExceptionPending()) {
                callbackResult->error_message = env.GetAndClearPendingException().Message();
                return;
            }

            napi_valuetype callbackType = napi_undefined;
            if (napi_typeof(env, callbackValue, &callbackType) != napi_ok) {
                callbackResult->error_message = "Failed to inspect stream callback return value";
                return;
            }

            if (callbackType == napi_boolean) {
                bool stopRequested = false;
                if (napi_get_value_bool(env, callbackValue, &stopRequested) == napi_ok) {
                    callbackResult->stop_requested = stopRequested;
                }
                return;
            }

            if (callbackType == napi_number) {
                double rowsUsed = 0;
                if (napi_get_value_double(env, callbackValue, &rowsUsed) == napi_ok) {
                    callbackResult->stop_requested = true;
                    callbackResult->rows_used = rowsUsed > 0 ? static_cast<size_t>(rowsUsed) : 0;
                }
                return;
            }

            if (callbackType == napi_object) {
                bool hasStop = false;
                if (napi_has_named_property(env, callbackValue, "stop", &hasStop) == napi_ok && hasStop) {
                    napi_value stopValue = nullptr;
                    if (napi_get_named_property(env, callbackValue, "stop", &stopValue) == napi_ok) {
                        bool stopRequested = false;
                        if (napi_get_value_bool(env, stopValue, &stopRequested) == napi_ok) {
                            callbackResult->stop_requested = stopRequested;
                        }
                    }
                }

                bool hasRowsUsed = false;
                if (napi_has_named_property(env, callbackValue, "rowsUsed", &hasRowsUsed) == napi_ok && hasRowsUsed) {
                    napi_value rowsUsedValue = nullptr;
                    if (napi_get_named_property(env, callbackValue, "rowsUsed", &rowsUsedValue) == napi_ok) {
                        double rowsUsed = 0;
                        if (napi_get_value_double(env, rowsUsedValue, &rowsUsed) == napi_ok) {
                            callbackResult->rows_used = rowsUsed > 0 ? static_cast<size_t>(rowsUsed) : 0;
                            callbackResult->stop_requested = true;
                        }
                    }
                }
            }
        });

        if (status != napi_ok) {
            result.ok = false;
            result.error_message = "Failed to deliver streamed rows to JavaScript";
            return result;
        }

        if (!callbackResult->error_message.empty()) {
            result.ok = false;
            result.error_message = callbackResult->error_message;
            return result;
        }

        if (callbackResult->rows_used > chunkRows->size()) {
            callbackResult->rows_used = chunkRows->size();
        }

        result.stop_requested = callbackResult->stop_requested;
        if (callbackResult->rows_used == 0) {
            result.last_consumed_row = chunkStartRow - 1;
        } else {
            result.last_consumed_row = chunkStartRow + static_cast<long>(callbackResult->rows_used) - 1;
        }

        return result;
    }

protected:
    void Execute() override {
        readstat_parser_t *parser = readstat_parser_init();

        if (offset > 0) {
            readstat_set_row_offset(parser, offset);
        }

        readstat_set_metadata_handler(parser, &stream_handle_metadata);
        readstat_set_variable_handler(parser, &stream_handle_variable);
        readstat_set_value_handler(parser, &stream_handle_value);

        readstat_error_t error = readstat_parse_sas7bdat(parser, filePath.c_str(), &context);
        readstat_parser_free(parser);

        parserReachedEnd = error == READSTAT_OK;

        if (error == READSTAT_OK && !context.chunk_rows.empty()) {
            stream_emit_result_t emitResult = EmitChunk(std::move(context.chunk_rows), context.chunk_start_row);
            context.chunk_rows.clear();

            if (!emitResult.ok) {
                ReleaseThreadSafeFunction();
                SetError(emitResult.error_message);
                return;
            }

            context.last_row = emitResult.last_consumed_row;
            if (emitResult.stop_requested) {
                context.stop_requested = true;
            }
        }

        ReleaseThreadSafeFunction();

        if (error != READSTAT_OK && !(error == READSTAT_ERROR_USER_ABORT && context.stop_requested)) {
            std::string errorMessage = "Failed to stream SAS7BDAT file: ";
            errorMessage += readstat_error_message(error);
            SetError(errorMessage);
        }
    }

    void OnOK() override {
        Napi::Env env = Env();
        Napi::Object result = Napi::Object::New(env);
        result.Set("lastRow", Napi::Number::New(env, context.last_row));
        result.Set("endReached", Napi::Boolean::New(env, parserReachedEnd));
        deferred.Resolve(result);
    }

    void OnError(const Napi::Error& e) override {
        deferred.Reject(e.Value());
    }

private:
    void ReleaseThreadSafeFunction() {
        if (!tsfnReleased) {
            tsfn.Release();
            tsfnReleased = true;
        }
    }

    std::string filePath;
    long offset;
    Napi::Promise::Deferred deferred;
    Napi::ThreadSafeFunction tsfn;
    stream_context_t context;
    bool parserReachedEnd;
    bool tsfnReleased;
};

static int stream_handle_metadata(readstat_metadata_t *metadata, void *ctx) {
    stream_context_t *context = (stream_context_t *)ctx;
    context->var_count = readstat_get_var_count(metadata);
    return READSTAT_HANDLER_OK;
}

static int stream_handle_variable(int index, readstat_variable_t *variable, const char *val_labels, void *ctx) {
    return READSTAT_HANDLER_OK;
}

static int stream_handle_value(int obs_index, readstat_variable_t *variable, readstat_value_t value, void *ctx) {
    stream_context_t *context = (stream_context_t *)ctx;
    int var_idx = readstat_variable_get_index(variable);

    if (var_idx == 0) {
        if (context->chunk_rows.empty()) {
            context->chunk_start_row = context->start_row + context->rows_seen;
        }
        context->chunk_rows.push_back(std::vector<ObsValue>(context->var_count));
    }

    context->chunk_rows.back()[var_idx] = makeObsValue(variable, value);

    if (var_idx == context->var_count - 1) {
        context->rows_seen++;
        context->last_row = context->start_row + context->rows_seen - 1;

        if (context->chunk_rows.size() >= context->chunk_size) {
            stream_emit_result_t emitResult = context->worker->EmitChunk(std::move(context->chunk_rows), context->chunk_start_row);
            context->chunk_rows.clear();

            if (!emitResult.ok) {
                context->error_message = emitResult.error_message;
                return READSTAT_HANDLER_ABORT;
            }

            context->last_row = emitResult.last_consumed_row;
            if (emitResult.stop_requested) {
                context->stop_requested = true;
                return READSTAT_HANDLER_ABORT;
            }
        }
    }

    return READSTAT_HANDLER_OK;
}

class ReadStatWorker : public Napi::AsyncWorker {
public:
    ReadStatWorker(
        Napi::Env& env,
        std::string filePath,
        long offset,
        long limit,
        const format_binding_config_t& formatConfig
    )
        : Napi::AsyncWorker(env),
          filePath(filePath),
          offset(offset),
          limit(limit),
          formatConfig(formatConfig),
          deferred(Napi::Promise::Deferred::New(env)) {}

    Napi::Promise GetPromise() { return deferred.Promise(); }

protected:
    void Execute() override {
        readstat_parser_t *parser = readstat_parser_init();

        if (offset > 0) readstat_set_row_offset(parser, offset);
        if (limit != -1) readstat_set_row_limit(parser, limit);

        readstat_set_metadata_handler(parser, &async_handle_metadata);
        readstat_set_variable_handler(parser, &async_handle_variable);
        readstat_set_value_handler(parser, &async_handle_value);

        readstat_error_t error = formatConfig.parse(parser, filePath.c_str(), &context);
        readstat_parser_free(parser);

        if (error != READSTAT_OK) {
            context.error_message = std::string("Failed to parse ") + formatConfig.file_format + " file: ";
            context.error_message += readstat_error_message(error);
            SetError(context.error_message);
        }
    }

    void OnOK() override {
        Napi::Env env = Env();
        Napi::Array result = Napi::Array::New(env, context.rows.size());

        for (size_t i = 0; i < context.rows.size(); i++) {
            Napi::Array row = Napi::Array::New(env, context.rows[i].size());
            for (size_t j = 0; j < context.rows[i].size(); j++) {
                row[j] = convertObsValue(env, context.rows[i][j]);
            }
            result[i] = row;
        }

        deferred.Resolve(result);
    }

    void OnError(const Napi::Error& e) override {
        deferred.Reject(e.Value());
    }

private:
    std::string filePath;
    long offset;
    long limit;
    const format_binding_config_t& formatConfig;
    async_context_t context;
    Napi::Promise::Deferred deferred;
};

// --- END ASYNC SUPPORT ---

static const char *safe_cstring(const char *value) {
    return value != nullptr ? value : "";
}

// Convert SAS format to a simplified type format
std::string getSASDataType(readstat_type_t type) {
    switch(type) {
        case READSTAT_TYPE_STRING:
            return "text";
        case READSTAT_TYPE_INT8:
        case READSTAT_TYPE_INT16:
        case READSTAT_TYPE_INT32:
            return "integer";
        case READSTAT_TYPE_FLOAT:
        case READSTAT_TYPE_DOUBLE:
            return "double";
        default:
            // Return character representation of the type
            return std::string(1, (char)type);
    }
}

// Callback for ReadStat
static int handle_metadata(readstat_metadata_t *metadata, void *ctx) {
    context_t *context = (context_t *)ctx;
    context->current_record = 0;
    context->var_count = readstat_get_var_count(metadata);
    context->current_var = 0;

    // Initialize storage for variable metadata
    context->var_names.resize(context->var_count);
    context->var_types.resize(context->var_count);

    return READSTAT_HANDLER_OK;
}

static int handle_variable(int index, readstat_variable_t *variable, const char *val_labels, void *ctx) {
    context_t *context = (context_t *)ctx;
    if (index < context->var_count) {
        context->var_names[index] = readstat_variable_get_name(variable);
        context->var_types[index] = readstat_variable_get_type(variable);
    }
    return READSTAT_HANDLER_OK;
}

static int handle_value(int obs_index, readstat_variable_t *variable,
                      readstat_value_t value, void *ctx) {
    context_t *context = (context_t *)ctx;

    // Update current variable index
    int var_idx = readstat_variable_get_index(variable);
    context->current_var = var_idx;

    // Create new row if this is the first variable in a row
    if (var_idx == 0) {
        context->rows.push_back(std::vector<Napi::Value>(context->var_count));
    }

    // Process the value
    Napi::Value jsValue;
    if (!readstat_value_is_missing(value, variable)) {
        switch(readstat_value_type(value)) {
            case READSTAT_TYPE_STRING:
                jsValue = Napi::String::New(context->env, readstat_string_value(value));
                break;
            case READSTAT_TYPE_INT8:
            case READSTAT_TYPE_INT16:
            case READSTAT_TYPE_INT32:
                jsValue = Napi::Number::New(context->env, readstat_int32_value(value));
                break;
            case READSTAT_TYPE_FLOAT:
            case READSTAT_TYPE_DOUBLE:
                jsValue = Napi::Number::New(context->env, readstat_double_value(value));
                break;
            default:
                jsValue = context->env.Null();
        }
    } else {
        jsValue = context->env.Null();
    }

    // Store in the array at the correct position
    context->rows.back()[var_idx] = jsValue;

    return READSTAT_HANDLER_OK;
}

// Enhanced metadata handler that processes most metadata directly
static int handle_metadata_only(readstat_metadata_t *metadata, void *ctx) {
    metadata_context_t *context = (metadata_context_t *)ctx;
    context->metadata = metadata;

    // Store the dataset properties directly in the context's dataset object

    // Set record count
    context->dataset.Set("records", Napi::Number::New(context->env,
                        readstat_get_row_count(metadata)));

    // Set dataset name
    const char* tableName = readstat_get_table_name(metadata);
    context->dataset.Set(
        "name",
        Napi::String::New(context->env, safe_cstring(tableName))
    );

    // Set dataset label if available
    const char* fileLabel = readstat_get_file_label(metadata);
    context->dataset.Set(
        "label",
        Napi::String::New(context->env, safe_cstring(fileLabel))
    );

    // Creation time
    time_t creationTime = readstat_get_creation_time(metadata);
    context->dataset.Set("CreationDateTime", Napi::Number::New(context->env, static_cast<double>(creationTime)));

    // Modification time
    time_t modTime = readstat_get_modified_time(metadata);
    context->dataset.Set("ModifiedDateTime", Napi::Number::New(context->env, static_cast<double>(modTime)));

    // Create and initialize columns array
    int var_count = readstat_get_var_count(metadata);
    context->columns = Napi::Array::New(context->env, var_count);
    context->dataset.Set("columns", context->columns);

    // Add optional fields from SAS metadata if available
    const int format_version = readstat_get_file_format_version(metadata);
    Napi::Object sourceSystem = Napi::Object::New(context->env);
    sourceSystem.Set("name", Napi::String::New(context->env, "SAS"));
    if (format_version) {
        sourceSystem.Set("version", Napi::String::New(context->env,
                          std::to_string(format_version)));
    }
    context->dataset.Set("sourceSystem", sourceSystem);

    // Add compression info if available
    const readstat_compress_t compression = readstat_get_compression(metadata);
    const char* compression_str = compressionTypeToString(compression);
    context->dataset.Set("compression", Napi::String::New(context->env, compression_str));

    // Add character encoding information
    const char* encoding = readstat_get_file_encoding(metadata);
    if (encoding != NULL) {
        context->dataset.Set("encoding", Napi::String::New(context->env, encoding));
    }
    // Add bit level information (32/64 bit)
    context->dataset.Set("is64Bit", Napi::Boolean::New(context->env,
                        readstat_get_file_format_is_64bit(metadata)));
    // Add file version information
    int file_format_version = readstat_get_file_format_version(metadata);
    if (file_format_version > 0) {
        context->dataset.Set("fileFormatVersion", Napi::Number::New(context->env, file_format_version));
    }

    return READSTAT_HANDLER_OK;
}

static int handle_variable_metadata(int index, readstat_variable_t *variable, const char *val_labels, void *ctx) {
    metadata_context_t *context = (metadata_context_t *)ctx;

    // Create column object directly and add it to the columns array
    Napi::Object column = Napi::Object::New(context->env);

    // Generate an OID based on the column name
    std::string oid = "IT." + std::string(readstat_variable_get_name(variable));
    column.Set("itemOID", Napi::String::New(context->env, oid));
    column.Set("name", Napi::String::New(context->env, readstat_variable_get_name(variable)));

    const char* label = readstat_variable_get_label(variable);
    column.Set("label", Napi::String::New(context->env,
               label ? label : readstat_variable_get_name(variable)));

    std::string dataType = getSASDataType(readstat_variable_get_type(variable));
    column.Set("dataType", Napi::String::New(context->env, dataType));

    // Include length if available
    size_t length = readstat_variable_get_storage_width(variable);
    if (length > 0) {
        column.Set("length", Napi::Number::New(context->env, length));
    }

    // Include format if available
    const char* format = readstat_variable_get_format(variable);
    if (format && strlen(format) > 0) {
        column.Set("displayFormat", Napi::String::New(context->env, format));
    }

    // Store the column in the array
    context->columns[index] = column;

    return READSTAT_HANDLER_OK;
}

// Get metadata from SAS7BDAT file - enhanced version with comprehensive metadata
static void apply_format_metadata(
    metadata_context_t *context,
    const format_binding_config_t& formatConfig,
    const std::string& filePath
) {
    // Get file name from path for the name field
    std::string fileName = filePath.substr(filePath.find_last_of("/\\") + 1);
    fileName = fileName.substr(0, fileName.find_last_of("."));

    Napi::Value currentNameValue = context->dataset.Get("name");
    std::string currentName =
        currentNameValue.IsString()
            ? currentNameValue.As<Napi::String>().Utf8Value()
            : std::string();

    if (currentName.empty()) {
        context->dataset.Set("name", Napi::String::New(context->env, fileName));
    }

    context->dataset.Set("filePath", Napi::String::New(context->env, filePath));
    context->dataset.Set("fileFormat", Napi::String::New(context->env, formatConfig.file_format));

    Napi::Object sourceSystem = Napi::Object::New(context->env);
    sourceSystem.Set("name", Napi::String::New(context->env, formatConfig.source_system));
    int formatVersion = readstat_get_file_format_version(context->metadata);
    if (formatVersion > 0) {
        sourceSystem.Set("version", Napi::String::New(context->env, std::to_string(formatVersion)));
    }
    context->dataset.Set("sourceSystem", sourceSystem);
}

static Napi::Value GetFormatMetadata(const Napi::CallbackInfo& info, const format_binding_config_t& formatConfig) {
    Napi::Env env = info.Env();

    if (info.Length() < 1) {
        Napi::TypeError::New(env, "Wrong number of arguments").ThrowAsJavaScriptException();
        return env.Null();
    }

    std::string filePath = info[0].As<Napi::String>().Utf8Value();
    metadata_context_t context(env);

    readstat_parser_t *parser = readstat_parser_init();
    readstat_set_metadata_handler(parser, &handle_metadata_only);
    readstat_set_variable_handler(parser, &handle_variable_metadata);

    readstat_error_t error = formatConfig.parse(parser, filePath.c_str(), &context);

    if (error != READSTAT_OK) {
        std::string errorMsg = std::string("Failed to parse ") + formatConfig.file_format + " metadata: ";
        errorMsg += readstat_error_message(error);
        readstat_parser_free(parser);
        Napi::Error::New(env, errorMsg).ThrowAsJavaScriptException();
        return env.Null();
    }

    apply_format_metadata(&context, formatConfig, filePath);

    readstat_parser_free(parser);

    return context.dataset;
}

Napi::Value GetSAS7BDATMetadata(const Napi::CallbackInfo& info) {
    return GetFormatMetadata(info, SAS7BDAT_FORMAT);
}

Napi::Value getDtaMetadata(const Napi::CallbackInfo& info) {
    return GetFormatMetadata(info, DTA_FORMAT);
}

Napi::Value getSavMetadata(const Napi::CallbackInfo& info) {
    return GetFormatMetadata(info, SAV_FORMAT);
}

Napi::Value getZsavMetadata(const Napi::CallbackInfo& info) {
    return GetFormatMetadata(info, ZSAV_FORMAT);
}

Napi::Value getPorMetadata(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    Napi::Value metadataValue = GetFormatMetadata(info, POR_FORMAT);
    if (env.IsExceptionPending() || !metadataValue.IsObject()) {
        return env.Null();
    }

    Napi::Object metadata = metadataValue.As<Napi::Object>();
    Napi::Value rowsValue = ReadFormat(info, POR_FORMAT);
    if (env.IsExceptionPending() || !rowsValue.IsArray()) {
        return env.Null();
    }

    metadata.Set("records", rowsValue.As<Napi::Array>().Length());
    return metadata;
}

// Node.js binding
static Napi::Value ReadFormat(const Napi::CallbackInfo& info, const format_binding_config_t& formatConfig) {
    Napi::Env env = info.Env();

    if (info.Length() < 1) {
        Napi::TypeError::New(env, "Wrong number of arguments").ThrowAsJavaScriptException();
        return env.Null();
    }

    std::string filePath = info[0].As<Napi::String>().Utf8Value();

    // Create context with proper initialization
    context_t context(env);

    // Initialize the parser
    readstat_parser_t *parser = readstat_parser_init();

    // Parse optional row offset parameter
    if (info.Length() > 1 && info[1].IsNumber()) {
        long row_offset = info[1].As<Napi::Number>().Int32Value();
        if (row_offset < 0) {
            Napi::RangeError::New(env, "Row offset must be non-negative").ThrowAsJavaScriptException();
            readstat_parser_free(parser);
            return env.Null();
        }
        readstat_set_row_offset(parser, row_offset);
    }

    // Parse optional row limit parameter
    if (info.Length() > 2 && info[2].IsNumber()) {
        long row_limit = info[2].As<Napi::Number>().Int32Value();
        if (row_limit < -1) {
            Napi::RangeError::New(env, "Row limit must be positive or -1 (for all records)").ThrowAsJavaScriptException();
            readstat_parser_free(parser);
            return env.Null();
        }
        if (row_limit != -1) {
            readstat_set_row_limit(parser, row_limit);
        }
    }

    readstat_set_metadata_handler(parser, &handle_metadata);
    readstat_set_variable_handler(parser, &handle_variable);
    readstat_set_value_handler(parser, &handle_value);

    readstat_error_t error = formatConfig.parse(parser, filePath.c_str(), &context);
    readstat_parser_free(parser);

    if (error != READSTAT_OK) {
        std::string errorMsg = std::string("Failed to parse ") + formatConfig.file_format + " file: ";
        errorMsg += readstat_error_message(error);
        Napi::Error::New(env, errorMsg).ThrowAsJavaScriptException();
        return env.Null();
    }

    // Create result array
    Napi::Array result = Napi::Array::New(env, context.rows.size());
    for (size_t i = 0; i < context.rows.size(); i++) {
        Napi::Array row = Napi::Array::New(env, context.rows[i].size());
        for (size_t j = 0; j < context.rows[i].size(); j++) {
            row[j] = context.rows[i][j];
        }
        result[i] = row;
    }

    return result;
}

Napi::Value ReadSas7bdat(const Napi::CallbackInfo& info) {
    return ReadFormat(info, SAS7BDAT_FORMAT);
}

Napi::Value ReadDta(const Napi::CallbackInfo& info) {
    return ReadFormat(info, DTA_FORMAT);
}

Napi::Value ReadSav(const Napi::CallbackInfo& info) {
    return ReadFormat(info, SAV_FORMAT);
}

Napi::Value ReadZsav(const Napi::CallbackInfo& info) {
    return ReadFormat(info, ZSAV_FORMAT);
}

Napi::Value ReadPor(const Napi::CallbackInfo& info) {
    return ReadFormat(info, POR_FORMAT);
}

// Async wrapper around ReadStat
static Napi::Value ReadFormatAsync(const Napi::CallbackInfo& info, const format_binding_config_t& formatConfig) {
    Napi::Env env = info.Env();

    if (info.Length() < 1) {
        Napi::TypeError::New(env, "Wrong number of arguments").ThrowAsJavaScriptException();
        return env.Null();
    }

    std::string filePath = info[0].As<Napi::String>().Utf8Value();
    long row_offset = 0;
    long row_limit = -1;

    // Parse optional row offset parameter
    if (info.Length() > 1 && info[1].IsNumber()) {
        row_offset = info[1].As<Napi::Number>().Int32Value();
        if (row_offset < 0) {
            Napi::RangeError::New(env, "Row offset must be non-negative").ThrowAsJavaScriptException();
            return env.Null();
        }
    }

    // Parse optional row limit parameter
    if (info.Length() > 2 && info[2].IsNumber()) {
        row_limit = info[2].As<Napi::Number>().Int32Value();
        if (row_limit < -1) {
            Napi::RangeError::New(env, "Row limit must be positive or -1 (for all records)").ThrowAsJavaScriptException();
            return env.Null();
        }
    }

    ReadStatWorker* worker = new ReadStatWorker(env, filePath, row_offset, row_limit, formatConfig);
    worker->Queue();
    return worker->GetPromise();
}

Napi::Value ReadSas7bdatAsync(const Napi::CallbackInfo& info) {
    return ReadFormatAsync(info, SAS7BDAT_FORMAT);
}

Napi::Value ReadDtaAsync(const Napi::CallbackInfo& info) {
    return ReadFormatAsync(info, DTA_FORMAT);
}

Napi::Value ReadSavAsync(const Napi::CallbackInfo& info) {
    return ReadFormatAsync(info, SAV_FORMAT);
}

Napi::Value ReadZsavAsync(const Napi::CallbackInfo& info) {
    return ReadFormatAsync(info, ZSAV_FORMAT);
}

Napi::Value ReadPorAsync(const Napi::CallbackInfo& info) {
    return ReadFormatAsync(info, POR_FORMAT);
}

static Napi::Value ReadFormatStream(const Napi::CallbackInfo& info, const format_binding_config_t& formatConfig) {
    Napi::Env env = info.Env();

    if (info.Length() < 1) {
        Napi::TypeError::New(env, "Wrong number of arguments").ThrowAsJavaScriptException();
        return env.Null();
    }

    std::string filePath = info[0].As<Napi::String>().Utf8Value();
    const bool isStreamingMode =
        info.Length() > 2 &&
        info[1].IsNumber() &&
        info[2].IsFunction();

    if (!isStreamingMode) {
        std::vector<std::string> projectedColumns;
        std::vector<long> selectedRows;
        long rowOffset = 0;

        try {
            if (info.Length() > 1) {
                projectedColumns = parseStringArray(info[1], "Selected columns");
            }
            if (info.Length() > 2) {
                selectedRows = parseLongArray(info[2], "Selected rows");
            }
        } catch (const Napi::Error& error) {
            error.ThrowAsJavaScriptException();
            return env.Null();
        }

        if (info.Length() > 3 && info[3].IsNumber()) {
            rowOffset = info[3].As<Napi::Number>().Int64Value();
            if (rowOffset < 0) {
                Napi::RangeError::New(env, "Row offset must be non-negative").ThrowAsJavaScriptException();
                return env.Null();
            }
        } else if (!selectedRows.empty()) {
            rowOffset = selectedRows.front();
        }

        selected_read_context_t context(rowOffset, projectedColumns, selectedRows);
        readstat_parser_t *parser = readstat_parser_init();
        if (rowOffset > 0) {
            readstat_set_row_offset(parser, rowOffset);
        }

        readstat_set_metadata_handler(parser, &selected_read_handle_metadata);
        readstat_set_variable_handler(parser, &selected_read_handle_variable);
        readstat_set_row_handler(parser, &selected_read_handle_row);
        readstat_set_value_handler(parser, &selected_read_handle_value);

        readstat_error_t error = formatConfig.parse(parser, filePath.c_str(), &context);
        readstat_parser_free(parser);

        if (error != READSTAT_OK && !(error == READSTAT_ERROR_USER_ABORT && !selectedRows.empty() && context.next_selected_row_index >= context.selected_rows.size())) {
            std::string errorMsg = std::string("Failed to parse ") + formatConfig.file_format + " file: ";
            errorMsg += readstat_error_message(error);
            Napi::Error::New(env, errorMsg).ThrowAsJavaScriptException();
            return env.Null();
        }

        Napi::Array result = Napi::Array::New(env, context.rows.size());
        for (size_t rowIndex = 0; rowIndex < context.rows.size(); rowIndex++) {
            Napi::Array row = Napi::Array::New(env, context.rows[rowIndex].size());
            for (size_t columnIndex = 0; columnIndex < context.rows[rowIndex].size(); columnIndex++) {
                row[columnIndex] = convertObsValue(env, context.rows[rowIndex][columnIndex]);
            }
            result[rowIndex] = row;
        }

        return result;
    }

    if (!info[1].IsNumber()) {
        Napi::TypeError::New(env, "Chunk size must be a positive integer").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (!info[2].IsFunction()) {
        Napi::TypeError::New(env, "Stream callback must be a function").ThrowAsJavaScriptException();
        return env.Null();
    }

    long chunk_size = info[1].As<Napi::Number>().Int64Value();
    if (chunk_size <= 0) {
        Napi::RangeError::New(env, "Chunk size must be a positive integer").ThrowAsJavaScriptException();
        return env.Null();
    }

    long row_offset = 0;
    if (info.Length() > 3 && info[3].IsNumber()) {
        row_offset = info[3].As<Napi::Number>().Int32Value();
        if (row_offset < 0) {
            Napi::RangeError::New(env, "Row offset must be non-negative").ThrowAsJavaScriptException();
            return env.Null();
        }
    }

    std::vector<std::string> projectedColumns;
    try {
        if (info.Length() > 4) {
            projectedColumns = parseStringArray(info[4], "Selected columns");
        }
    } catch (const Napi::Error& error) {
        error.ThrowAsJavaScriptException();
        return env.Null();
    }

    sync_stream_context_t context(
        env,
        info[2].As<Napi::Function>(),
        static_cast<size_t>(chunk_size),
        row_offset,
        projectedColumns
    );

    readstat_parser_t *parser = readstat_parser_init();
    if (row_offset > 0) {
        readstat_set_row_offset(parser, row_offset);
    }

    readstat_set_metadata_handler(parser, &sync_stream_handle_metadata);
    readstat_set_variable_handler(parser, &sync_stream_handle_variable);
    readstat_set_value_handler(parser, &sync_stream_handle_value);

    readstat_error_t error = formatConfig.parse(parser, filePath.c_str(), &context);
    readstat_parser_free(parser);

    bool endReached = error == READSTAT_OK;

    if (error == READSTAT_OK && !context.chunk_rows.empty()) {
        if (!emit_sync_stream_chunk(&context)) {
            Napi::Error::New(env, context.error_message).ThrowAsJavaScriptException();
            return env.Null();
        }
    }

    if (error != READSTAT_OK && !(error == READSTAT_ERROR_USER_ABORT && context.stop_requested)) {
        if (!context.error_message.empty()) {
            Napi::Error::New(env, context.error_message).ThrowAsJavaScriptException();
        } else {
            std::string errorMessage = std::string("Failed to stream ") + formatConfig.file_format + " file: ";
            errorMessage += readstat_error_message(error);
            Napi::Error::New(env, errorMessage).ThrowAsJavaScriptException();
        }
        return env.Null();
    }

    Napi::Object result = Napi::Object::New(env);
    result.Set("lastRow", Napi::Number::New(env, context.last_row));
    result.Set("endReached", Napi::Boolean::New(env, endReached));
    return result;
}

Napi::Value ReadSas7bdatStream(const Napi::CallbackInfo& info) {
    return ReadFormatStream(info, SAS7BDAT_FORMAT);
}

Napi::Value ReadDtaStream(const Napi::CallbackInfo& info) {
    return ReadFormatStream(info, DTA_FORMAT);
}

Napi::Value ReadSavStream(const Napi::CallbackInfo& info) {
    return ReadFormatStream(info, SAV_FORMAT);
}

Napi::Value ReadZsavStream(const Napi::CallbackInfo& info) {
    return ReadFormatStream(info, ZSAV_FORMAT);
}

Napi::Value ReadPorStream(const Napi::CallbackInfo& info) {
    return ReadFormatStream(info, POR_FORMAT);
}

Napi::Object Init(Napi::Env env, Napi::Object exports) {
    exports.Set("readSas7bdat", Napi::Function::New(env, ReadSas7bdat));
    exports.Set("readSas7bdatAsync", Napi::Function::New(env, ReadSas7bdatAsync));
    exports.Set("readSas7bdatStream", Napi::Function::New(env, ReadSas7bdatStream));
    exports.Set("getSAS7BDATMetadata", Napi::Function::New(env, GetSAS7BDATMetadata));
    exports.Set("readDta", Napi::Function::New(env, ReadDta));
    exports.Set("readDtaAsync", Napi::Function::New(env, ReadDtaAsync));
    exports.Set("readDtaStream", Napi::Function::New(env, ReadDtaStream));
    exports.Set("getDtaMetadata", Napi::Function::New(env, getDtaMetadata));
    exports.Set("readSav", Napi::Function::New(env, ReadSav));
    exports.Set("readSavAsync", Napi::Function::New(env, ReadSavAsync));
    exports.Set("readSavStream", Napi::Function::New(env, ReadSavStream));
    exports.Set("getSavMetadata", Napi::Function::New(env, getSavMetadata));
    exports.Set("readZsav", Napi::Function::New(env, ReadZsav));
    exports.Set("readZsavAsync", Napi::Function::New(env, ReadZsavAsync));
    exports.Set("readZsavStream", Napi::Function::New(env, ReadZsavStream));
    exports.Set("getZsavMetadata", Napi::Function::New(env, getZsavMetadata));
    exports.Set("readPor", Napi::Function::New(env, ReadPor));
    exports.Set("readPorAsync", Napi::Function::New(env, ReadPorAsync));
    exports.Set("readPorStream", Napi::Function::New(env, ReadPorStream));
    exports.Set("getPorMetadata", Napi::Function::New(env, getPorMetadata));
    return exports;
}

NODE_API_MODULE(readstat_binding, Init)