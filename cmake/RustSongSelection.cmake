option(BMS_ENABLE_RUST_SONG_SELECTION "Use the Rust song-selection model as the default implementation" ON)

set(BMS_SONG_SELECTION_SOURCE "${PROJECT_SOURCE_DIR}/src/SongSelection.cpp")
set(BMS_SONG_SELECTION_INCLUDE_DIRS "")
if(BMS_ENABLE_RUST_SONG_SELECTION)
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR NOT MSVC)
        message(FATAL_ERROR "Rust song-selection experiment currently supports Windows x64 MSVC only")
    endif()

    if(NOT BMS_CARGO_EXECUTABLE)
        find_program(BMS_CARGO_EXECUTABLE cargo REQUIRED HINTS
            "$ENV{CARGO_HOME}/bin"
            "$ENV{USERPROFILE}/.cargo/bin"
            "${PROJECT_SOURCE_DIR}/build-rust-tools/cargo/bin")
    endif()
    set(default_cargo_home "$ENV{CARGO_HOME}")
    set(default_rustup_home "$ENV{RUSTUP_HOME}")
    if(NOT default_cargo_home AND EXISTS "${PROJECT_SOURCE_DIR}/build-rust-tools/cargo")
        set(default_cargo_home "${PROJECT_SOURCE_DIR}/build-rust-tools/cargo")
    endif()
    if(NOT default_rustup_home AND EXISTS "${PROJECT_SOURCE_DIR}/build-rust-tools/rustup")
        set(default_rustup_home "${PROJECT_SOURCE_DIR}/build-rust-tools/rustup")
    endif()
    if(NOT default_cargo_home)
        set(default_cargo_home "$ENV{USERPROFILE}/.cargo")
    endif()
    if(NOT default_rustup_home)
        set(default_rustup_home "$ENV{USERPROFILE}/.rustup")
    endif()
    set(BMS_CARGO_HOME "${default_cargo_home}" CACHE PATH "Project-local Cargo home")
    set(BMS_RUSTUP_HOME "${default_rustup_home}" CACHE PATH "Project-local Rustup home")

    set(rust_song_selection_crate "${PROJECT_SOURCE_DIR}/rust/song_selection")
    set(rust_song_selection_target "${CMAKE_BINARY_DIR}/rust-song-selection-target")
    set(rust_song_selection_lib "${rust_song_selection_target}/x86_64-pc-windows-msvc/release/bms_song_selection_rust.lib")
    add_custom_command(OUTPUT "${rust_song_selection_lib}"
        COMMAND ${CMAKE_COMMAND} -E env "CARGO_HOME=${BMS_CARGO_HOME}" "RUSTUP_HOME=${BMS_RUSTUP_HOME}"
            "${BMS_CARGO_EXECUTABLE}" build --release --locked --offline
            --target x86_64-pc-windows-msvc --target-dir "${rust_song_selection_target}"
        WORKING_DIRECTORY "${rust_song_selection_crate}"
        DEPENDS "${rust_song_selection_crate}/src/lib.rs" "${rust_song_selection_crate}/Cargo.toml"
            "${rust_song_selection_crate}/Cargo.lock" "${rust_song_selection_crate}/.cargo/config.toml"
            "${rust_song_selection_crate}/include/BmsSongSelectionAbi.h"
            "${PROJECT_SOURCE_DIR}/rust-toolchain.toml"
        VERBATIM)
    add_custom_target(bms_rust_song_selection_build DEPENDS "${rust_song_selection_lib}")
    add_library(bms_rust_song_selection_native STATIC IMPORTED GLOBAL)
    set_target_properties(bms_rust_song_selection_native PROPERTIES IMPORTED_LOCATION "${rust_song_selection_lib}")
    add_dependencies(bms_rust_song_selection_native bms_rust_song_selection_build)

    # The Rust static library is built with the static release CRT. Keep the
    # C++ implementation for Debug, whose MSVC runtime is /MTd.
    set(BMS_SONG_SELECTION_SOURCE
        "$<$<CONFIG:Debug>:${PROJECT_SOURCE_DIR}/src/SongSelection.cpp>"
        "$<$<NOT:$<CONFIG:Debug>>:${PROJECT_SOURCE_DIR}/src/SongSelectionRust.cpp>")
    set(BMS_SONG_SELECTION_INCLUDE_DIRS "${rust_song_selection_crate}/include")

    # The Rust static library uses /MT. Keep Debug targets on a C++-only path.
    add_executable(BmsRustSongSelectionBuildTest tests/RustSongSelectionBuildTest.cpp)
    target_compile_definitions(BmsRustSongSelectionBuildTest PRIVATE
        "$<$<NOT:$<CONFIG:Debug>>:BMS_RUST_SONG_SELECTION_BUILD_CHECK>")
    target_link_libraries(BmsRustSongSelectionBuildTest PRIVATE
        "$<$<NOT:$<CONFIG:Debug>>:bms_rust_song_selection_native>"
        "$<$<NOT:$<CONFIG:Debug>>:ws2_32;userenv;ntdll;dbghelp;kernel32>")
    add_test(NAME BmsRustSongSelectionBuildTest COMMAND BmsRustSongSelectionBuildTest)
    set_tests_properties(BmsRustSongSelectionBuildTest PROPERTIES CONFIGURATIONS Release)

    add_executable(BmsRustSongSelectionAbiTests tests/RustSongSelectionAbiTests.cpp)
    target_include_directories(BmsRustSongSelectionAbiTests PRIVATE
        "${PROJECT_SOURCE_DIR}/rust/song_selection/include")
    target_compile_definitions(BmsRustSongSelectionAbiTests PRIVATE
        "$<$<NOT:$<CONFIG:Debug>>:BMS_RUST_SONG_SELECTION_BUILD_CHECK>")
    target_link_libraries(BmsRustSongSelectionAbiTests PRIVATE
        "$<$<NOT:$<CONFIG:Debug>>:bms_rust_song_selection_native>"
        "$<$<NOT:$<CONFIG:Debug>>:ws2_32;userenv;ntdll;dbghelp;kernel32>")
    add_test(NAME BmsRustSongSelectionAbiTests COMMAND BmsRustSongSelectionAbiTests)
    set_tests_properties(BmsRustSongSelectionAbiTests PROPERTIES CONFIGURATIONS Release)
endif()
