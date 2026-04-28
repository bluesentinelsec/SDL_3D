set(SDL3D_PONG_ASSET_FILES
    images/splash-logo.jpg
    pong.game.json
    scenes/options.scene.json
    scenes/play.scene.json
    scenes/splash.scene.json
    scenes/title.scene.json
    scripts/pong.lua)

sdl3d_add_embedded_asset_pack(pong_assets
    ROOT "${CMAKE_CURRENT_SOURCE_DIR}/data"
    SYMBOL sdl3d_pong_assets
    FILES ${SDL3D_PONG_ASSET_FILES})

add_executable(pong_demo
    main.c)
target_link_libraries(pong_demo PRIVATE SDL3D::sdl3d)
target_link_libraries(pong_demo PRIVATE pong_assets)
target_compile_features(pong_demo PRIVATE c_std_17)
target_compile_definitions(pong_demo PRIVATE
    SDL3D_MEDIA_DIR="${CMAKE_SOURCE_DIR}/media"
    SDL3D_PONG_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
    SDL3D_PONG_DATA_ASSET_PATH="asset://pong.game.json"
    SDL3D_PONG_EMBEDDED_ASSETS=1)
set_target_properties(pong_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/demos/pong")

if(TARGET sdl3d_pack)
    sdl3d_add_asset_pack(pong_asset_pack
        ROOT "${CMAKE_CURRENT_SOURCE_DIR}/data"
        OUTPUT "${CMAKE_BINARY_DIR}/demos/pong/pong.sdl3dpak"
        FILES ${SDL3D_PONG_ASSET_FILES})
    add_dependencies(pong_demo pong_asset_pack)
endif()

if(NOT WIN32)
    target_link_libraries(pong_demo PRIVATE m)
endif()
