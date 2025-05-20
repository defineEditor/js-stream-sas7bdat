// binding/readstat_binding.cc
#include <napi.h>
#include <vector>
#include <map>
#include <string>
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

    // Set dataset label if available
    const char* fileLabel = readstat_get_file_label(metadata);
    const char* tableLabel = readstat_get_table_name(metadata);
    context->dataset.Set("label", Napi::String::New(context->env, tableLabel ? tableLabel : fileLabel));

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
Napi::Value GetSAS7BDATMetadata(const Napi::CallbackInfo& info) {
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

    readstat_error_t error = readstat_parse_sas7bdat(parser, filePath.c_str(), &context);

    if (error != READSTAT_OK) {
        std::string errorMsg = "Failed to parse SAS7BDAT metadata: ";
        errorMsg += readstat_error_message(error);
        readstat_parser_free(parser);
        Napi::Error::New(env, errorMsg).ThrowAsJavaScriptException();
        return env.Null();
    }

    // Get file name from path for the name field
    std::string fileName = filePath.substr(filePath.find_last_of("/\\") + 1);
    fileName = fileName.substr(0, fileName.find_last_of("."));
    context.dataset.Set("name", Napi::String::New(env, fileName));

    // Add file path for reference
    context.dataset.Set("filePath", Napi::String::New(env, filePath));

    // Add file format information
    context.dataset.Set("fileFormat", Napi::String::New(env, "SAS7BDAT"));

    // Cleanup
    readstat_parser_free(parser);

    return context.dataset;
}

// Node.js binding
Napi::Value ReadSas7bdat(const Napi::CallbackInfo& info) {
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

    readstat_error_t error = readstat_parse_sas7bdat(parser, filePath.c_str(), &context);
    readstat_parser_free(parser);

    if (error != READSTAT_OK) {
        std::string errorMsg = "Failed to parse SAS7BDAT file: ";
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

Napi::Object Init(Napi::Env env, Napi::Object exports) {
    exports.Set("readSas7bdat", Napi::Function::New(env, ReadSas7bdat));
    exports.Set("getSAS7BDATMetadata", Napi::Function::New(env, GetSAS7BDATMetadata));
    return exports;
}

NODE_API_MODULE(readstat_binding, Init)