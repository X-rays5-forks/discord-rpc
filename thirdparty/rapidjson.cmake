set(RAPIDJSON_BUILD_DOC OFF CACHE BOOL "Disable RapidJSON tests" FORCE)
set(RAPIDJSON_BUILD_EXAMPLES OFF CACHE BOOL "Disable RapidJSON tests" FORCE)
set(RAPIDJSON_BUILD_TESTS OFF CACHE BOOL "Disable RapidJSON tests" FORCE)
cpmaddpackage("gh:Tencent/rapidjson#24b5e7a8b27f42fa16b96fc70aade9106cf7102f")


target_include_directories(discord-rpc PRIVATE ${rapidjson_SOURCE_DIR}/include)

