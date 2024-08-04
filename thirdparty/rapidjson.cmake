cpmaddpackage("gh:Tencent/rapidjson#ab1842a2dae061284c0a62dca1cc6d5e7e37e346")

target_include_directories(discord-rpc PRIVATE ${rapidjson_SOURCE_DIR}/include)