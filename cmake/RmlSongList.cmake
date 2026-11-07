option(BMS_ENABLE_RMLUI_SONG_LIST "Render the song list with RmlUi" ON)
option(BMS_ENABLE_RMLUI "Enable the shared RmlUi runtime" ON)
option(BMS_ENABLE_RMLUI_SETTINGS "Enable RmlUi settings with native fallback" ON)
option(BMS_ENABLE_SONG_LIST_DIAGNOSTICS "Emit per-stage RmlUi song-list timings to the debugger" OFF)
set(BMS_RMLUI_ACTIVE OFF)
if(BMS_ENABLE_RMLUI AND (BMS_ENABLE_RMLUI_SONG_LIST OR BMS_ENABLE_RMLUI_SETTINGS))
    set(BMS_RMLUI_ACTIVE ON)
endif()

add_library(bms_song_list_ui STATIC "${PROJECT_SOURCE_DIR}/src/RmlSongListRenderer.cpp"
    "${PROJECT_SOURCE_DIR}/src/RmlUiRuntime.cpp" "${PROJECT_SOURCE_DIR}/src/RmlSettingsHost.cpp")
target_include_directories(bms_song_list_ui PUBLIC "${PROJECT_SOURCE_DIR}/src")

if(BMS_RMLUI_ACTIVE)
    if(NOT EXISTS "${PROJECT_SOURCE_DIR}/third_party/RmlUi/CMakeLists.txt" OR
       NOT EXISTS "${PROJECT_SOURCE_DIR}/third_party/freetype/CMakeLists.txt")
        message(FATAL_ERROR "RmlUi dependencies are missing. Run git submodule update --init --recursive, or configure BMS_ENABLE_RMLUI=OFF.")
    endif()
    enable_language(C)
    # PerformanceLibraries applies project-wide link_libraries(). External
    # projects export their own targets, so do not inherit our interface target.
    get_directory_property(bms_saved_directory_links LINK_LIBRARIES)
    set_property(DIRECTORY PROPERTY LINK_LIBRARIES "")
    # Keep the existing /MT and /MTd policy throughout all three libraries.
    set(BUILD_SHARED_LIBS OFF)
    set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
    add_subdirectory("${PROJECT_SOURCE_DIR}/third_party/freetype" "${CMAKE_BINARY_DIR}/freetype" EXCLUDE_FROM_ALL)
    add_library(Freetype::Freetype ALIAS freetype)
    set(FREETYPE_LIBRARY freetype)
    set(FREETYPE_INCLUDE_DIR_ft2build "${PROJECT_SOURCE_DIR}/third_party/freetype/include")
    set(FREETYPE_INCLUDE_DIR_freetype2 "${PROJECT_SOURCE_DIR}/third_party/freetype/include")
    set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
    set(RMLUI_LUA_BINDINGS OFF CACHE BOOL "" FORCE)
    set(RMLUI_FONT_ENGINE freetype CACHE STRING "" FORCE)
    add_subdirectory("${PROJECT_SOURCE_DIR}/third_party/RmlUi" "${CMAKE_BINARY_DIR}/rmlui" EXCLUDE_FROM_ALL)
    set_property(DIRECTORY PROPERTY LINK_LIBRARIES "${bms_saved_directory_links}")

    target_sources(bms_song_list_ui PRIVATE "${PROJECT_SOURCE_DIR}/third_party/RmlUi/Backends/RmlUi_Renderer_DX11.cpp"
        "${PROJECT_SOURCE_DIR}/third_party/RmlUi/Backends/RmlUi_Platform_Win32.cpp")
    target_include_directories(bms_song_list_ui PRIVATE "${PROJECT_SOURCE_DIR}/third_party/RmlUi/Backends")
    target_compile_definitions(bms_song_list_ui PRIVATE BMS_ENABLE_RMLUI)
    if(BMS_ENABLE_RMLUI_SONG_LIST)
        target_compile_definitions(bms_song_list_ui PRIVATE BMS_ENABLE_RMLUI_SONG_LIST)
    endif()
    if(BMS_ENABLE_RMLUI_SETTINGS)
        target_compile_definitions(bms_song_list_ui PRIVATE BMS_ENABLE_RMLUI_SETTINGS)
    endif()
    if(BMS_ENABLE_SONG_LIST_DIAGNOSTICS)
        target_compile_definitions(bms_song_list_ui PRIVATE BMS_PERF_DIAGNOSTICS)
    endif()
    target_link_libraries(bms_song_list_ui PRIVATE RmlUi::Core d3d11 dxgi d3dcompiler msimg32 gdiplus imm32)
endif()

function(bms_attach_song_list_ui target)
    target_link_libraries(${target} PRIVATE bms_song_list_ui)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/Assets"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/Assets/startup-splash.png"
            "$<TARGET_FILE_DIR:${target}>/Assets/startup-splash.png")
    if(BMS_RMLUI_ACTIVE)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/Assets/Fonts"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/Assets/Fonts" "$<TARGET_FILE_DIR:${target}>/Assets/Fonts")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/Assets/UI"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PROJECT_SOURCE_DIR}/Assets/UI/song-list.rml"
                "${PROJECT_SOURCE_DIR}/Assets/UI/song-list.rcss"
                "${PROJECT_SOURCE_DIR}/Assets/UI/settings-probe.rml"
                "${PROJECT_SOURCE_DIR}/Assets/UI/settings.rml"
                "${PROJECT_SOURCE_DIR}/Assets/UI/settings.rcss"
                "$<TARGET_FILE_DIR:${target}>/Assets/UI"
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/Assets/Licenses"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PROJECT_SOURCE_DIR}/third_party/RmlUi/LICENSE.txt"
                "$<TARGET_FILE_DIR:${target}>/Assets/Licenses/RmlUi.txt"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PROJECT_SOURCE_DIR}/third_party/RmlUi/Include/RmlUi/Core/Containers/LICENSE.txt"
                "$<TARGET_FILE_DIR:${target}>/Assets/Licenses/RmlUi-Containers.txt"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PROJECT_SOURCE_DIR}/third_party/freetype/docs/FTL.TXT"
                "$<TARGET_FILE_DIR:${target}>/Assets/Licenses/FreeType.txt"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PROJECT_SOURCE_DIR}/third_party/freetype/LICENSE.TXT"
                "$<TARGET_FILE_DIR:${target}>/Assets/Licenses/FreeType-Licenses.txt")
    endif()
endfunction()
