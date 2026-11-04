option(BMS_USE_RUST_BACKEND "Enable optional Rust line, JSON, title, and chart discovery helpers (Release only)" OFF)
if(BMS_USE_RUST_BACKEND)
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR NOT MSVC)
        message(FATAL_ERROR "Rust experiment currently supports Windows x64 MSVC only")
    endif()
    find_program(BMS_CARGO_EXECUTABLE cargo REQUIRED HINTS "$ENV{CARGO_HOME}/bin")
    set(default_cargo_home "$ENV{CARGO_HOME}")
    set(default_rustup_home "$ENV{RUSTUP_HOME}")
    if(NOT default_cargo_home)
        set(default_cargo_home "$ENV{USERPROFILE}/.cargo")
    endif()
    if(NOT default_rustup_home)
        set(default_rustup_home "$ENV{USERPROFILE}/.rustup")
    endif()
    set(BMS_CARGO_HOME "${default_cargo_home}" CACHE PATH "Project-local Cargo home")
    set(BMS_RUSTUP_HOME "${default_rustup_home}" CACHE PATH "Project-local Rustup home")
    set(rust_crate "${PROJECT_SOURCE_DIR}/rust/bms_backend")
    set(rust_target "${CMAKE_BINARY_DIR}/rust-target")
    set(rust_lib "${rust_target}/x86_64-pc-windows-msvc/release/bms_backend.lib")
    add_custom_command(OUTPUT "${rust_lib}"
        COMMAND ${CMAKE_COMMAND} -E env "CARGO_HOME=${BMS_CARGO_HOME}" "RUSTUP_HOME=${BMS_RUSTUP_HOME}"
            "${BMS_CARGO_EXECUTABLE}" build --release --locked --offline
            --target x86_64-pc-windows-msvc --target-dir "${rust_target}"
        WORKING_DIRECTORY "${rust_crate}"
        DEPENDS "${rust_crate}/src/lib.rs" "${rust_crate}/Cargo.toml" "${rust_crate}/Cargo.lock"
            "${rust_crate}/.cargo/config.toml" "${PROJECT_SOURCE_DIR}/rust-toolchain.toml"
        VERBATIM)
    add_custom_target(bms_rust_build DEPENDS "${rust_lib}")
    add_library(bms_rust_native STATIC IMPORTED GLOBAL)
    set_target_properties(bms_rust_native PROPERTIES IMPORTED_LOCATION "${rust_lib}")
    add_dependencies(bms_rust_native bms_rust_build)
    # Rust release uses /MT; Debug keeps the original /MTd C++ backend.
    target_compile_definitions(bms_core PUBLIC "$<$<NOT:$<CONFIG:Debug>>:BMS_USE_RUST_BACKEND>")
    target_link_libraries(bms_core PUBLIC "$<$<NOT:$<CONFIG:Debug>>:bms_rust_native>"
        "$<$<NOT:$<CONFIG:Debug>>:ws2_32;userenv;ntdll;dbghelp>")
    set_target_properties(BmsPlayer PROPERTIES OUTPUT_NAME "BmsPlayer-Rust")
    set_target_properties(BmsPlayerTest PROPERTIES OUTPUT_NAME "BmsPlayer-Rust-Test")
    add_executable(BmsRustBackendTests tests/RustBackendTests.cpp)
    target_link_libraries(BmsRustBackendTests PRIVATE bms_core)
    add_test(NAME BmsRustBackendTests COMMAND BmsRustBackendTests "${CMAKE_BINARY_DIR}/RustFixtures")
    set_tests_properties(BmsRustBackendTests PROPERTIES CONFIGURATIONS Release)
endif()
