CPMAddPackage("gh:stephenberry/glaze@5.6.0")

target_link_libraries(discord-rpc PRIVATE glaze::glaze)
