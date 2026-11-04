#include <cstdint>

#ifdef BMS_RUST_SONG_SELECTION_BUILD_CHECK
extern "C" std::uint32_t bms_song_selection_abi_version();
extern "C" std::uint32_t bms_song_selection_probe(std::uint32_t value);
#endif

int main() {
#ifdef BMS_RUST_SONG_SELECTION_BUILD_CHECK
    return bms_song_selection_abi_version() == 2 &&
        bms_song_selection_probe(0) == 7 &&
        bms_song_selection_probe(12) == 379 &&
        bms_song_selection_probe(UINT32_MAX) == UINT32_MAX - 23 ? 0 : 1;
#else
    // Debug keeps the C++ build path and does not link the Release Rust CRT.
    return 0;
#endif
}
