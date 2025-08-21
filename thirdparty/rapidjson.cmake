cpmaddpackage("gh:Tencent/rapidjson#24b5e7a8b27f42fa16b96fc70aade9106cf7102f")


target_include_directories(discord-rpc PRIVATE ${rapidjson_SOURCE_DIR}/include)
