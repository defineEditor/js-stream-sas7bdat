{
  "targets": [
    {
      "target_name": "readstat_binding",
      "sources": [
        "src/binding/readstat_binding.cc",
        "src/binding/ReadStat/src/CKHashTable.c",
        "src/binding/ReadStat/src/readstat_bits.c",
        "src/binding/ReadStat/src/readstat_convert.c",
        "src/binding/ReadStat/src/readstat_error.c",
        "src/binding/ReadStat/src/readstat_io_unistd.c",
        "src/binding/ReadStat/src/readstat_malloc.c",
        "src/binding/ReadStat/src/readstat_metadata.c",
        "src/binding/ReadStat/src/readstat_parser.c",
        "src/binding/ReadStat/src/readstat_value.c",
        "src/binding/ReadStat/src/readstat_variable.c",
        "src/binding/ReadStat/src/readstat_writer.c",
        "src/binding/ReadStat/src/sas/ieee.c",
        "src/binding/ReadStat/src/sas/readstat_sas.c",
        "src/binding/ReadStat/src/sas/readstat_sas7bcat_read.c",
        "src/binding/ReadStat/src/sas/readstat_sas7bcat_write.c",
        "src/binding/ReadStat/src/sas/readstat_sas7bdat_read.c",
        "src/binding/ReadStat/src/sas/readstat_sas7bdat_write.c",
        "src/binding/ReadStat/src/sas/readstat_sas_rle.c",
        "src/binding/ReadStat/src/sas/readstat_xport.c",
        "src/binding/ReadStat/src/sas/readstat_xport_parse_format.c",
        "src/binding/ReadStat/src/sas/readstat_xport_read.c",
        "src/binding/ReadStat/src/sas/readstat_xport_write.c"
      ],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")",
        "src/binding/ReadStat/src"
      ],
      "dependencies": [
        "<!(node -p \"require('node-addon-api').gyp\")"
      ],
      "cflags!": ["-fno-exceptions"],
      "cflags_cc!": ["-fno-exceptions"],
      "defines": ["NAPI_DISABLE_CPP_EXCEPTIONS"],
      "conditions": [
        ["OS=='win'", {
          "msvs_settings": {
            "VCCLCompilerTool": {
              "ExceptionHandling": 1
            }
          }
        }],
        ["OS=='mac'", {
          "xcode_settings": {
            "GCC_ENABLE_CPP_EXCEPTIONS": "YES",
            "CLANG_CXX_LIBRARY": "libc++",
            "MACOSX_DEPLOYMENT_TARGET": "10.15"
          }
        }]
      ]
    }
  ]
}
