# NVIDIA NVAPI

Unmodified headers and License.txt from NVIDIA/nvapi commit
`70d337db9186e968eab622f7e786de7e437faf3d` (MIT).
Source: https://github.com/NVIDIA/nvapi/tree/70d337db9186e968eab622f7e786de7e437faf3d

The application resolves the published interface IDs in nvapi_interface.h from
System32/nvapi64.dll (or nvapi.dll on x86). No driver DLL/import library is shipped.
The driver runtime is initialized once and kept loaded until process termination;
per-device release turns the mode off before discarding the borrowed D3D device.
Build with BMS_ENABLE_NVIDIA_REFLEX=OFF to remove SDK header/runtime integration.
