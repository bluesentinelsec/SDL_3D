file(GLOB SDL3D_PONG_AUDIO_FILES CONFIGURE_DEPENDS
    RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/data"
    "${CMAKE_CURRENT_SOURCE_DIR}/data/audio/music/*"
    "${CMAKE_CURRENT_SOURCE_DIR}/data/audio/ui/*")
list(SORT SDL3D_PONG_AUDIO_FILES)

set(SDL3D_PONG_ASSET_FILES
    ${SDL3D_PONG_AUDIO_FILES}
    images/ball-texture.png
    images/splash-logo.jpg
    pong.game.json
    scenes/multiplayer.scene.json
    scenes/multiplayer_direct_connect.scene.json
    scenes/multiplayer_discovery.scene.json
    scenes/multiplayer_join.scene.json
    scenes/multiplayer_lan.scene.json
    scenes/multiplayer_lobby.scene.json
    scenes/play.scene.json
    scenes/splash.scene.json
    scenes/title.scene.json
    shaders/splash_logo_melt.frag.glsl
    scripts/pong.lua)

sdl3d_add_embedded_asset_pack(pong_assets
    ROOT "${CMAKE_CURRENT_SOURCE_DIR}/data"
    SYMBOL sdl3d_pong_assets
    FILES ${SDL3D_PONG_ASSET_FILES})

add_executable(pong_demo
    "${CMAKE_SOURCE_DIR}/tools/sdl3d_runner.c")
target_link_libraries(pong_demo PRIVATE SDL3D::sdl3d)
target_link_libraries(pong_demo PRIVATE pong_assets)
target_compile_features(pong_demo PRIVATE c_std_17)
target_compile_definitions(pong_demo PRIVATE
    SDL3D_MEDIA_DIR="${CMAKE_SOURCE_DIR}/media"
    SDL3D_RUNNER_EMBEDDED_ASSETS=1
    SDL3D_RUNNER_EMBEDDED_ASSETS_DATA=sdl3d_pong_assets
    SDL3D_RUNNER_EMBEDDED_ASSETS_SIZE=sdl3d_pong_assets_size
    SDL3D_RUNNER_DEFAULT_DATA_ASSET_PATH="asset://pong.game.json"
    SDL3D_RUNNER_DEFAULT_EMBEDDED=1)
set_target_properties(pong_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/demos/pong")

if(TARGET sdl3d_pack)
    sdl3d_add_asset_pack(pong_asset_pack
        ROOT "${CMAKE_CURRENT_SOURCE_DIR}/data"
        OUTPUT "${CMAKE_BINARY_DIR}/demos/pong/pong.sdl3dpak"
        FILES ${SDL3D_PONG_ASSET_FILES})
    add_dependencies(pong_demo pong_asset_pack)
endif()
