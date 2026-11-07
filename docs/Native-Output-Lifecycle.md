# Native GPU output and returning to GDI screens

A flip-model swap chain must not target the main application HWND: after its first
presentation Windows can ignore later GDI draws to that HWND, even after release.
Reference: https://learn.microsoft.com/en-us/windows/desktop/api/dxgi/ne-dxgi-dxgi_swap_effect

With DirectComposition OFF, the renderer now owns a separate disabled, transparent-
to-input child HWND. Only that HWND receives flip-model output. Reset destroys it;
the main HWND remains usable for the browser, result/loading screens and CPU output.
The DirectComposition ON path continues to use a composition visual.

QUIT clears playing/paused/GPU-frame state before releasing the output, then redraws
the full main window and its controls synchronously. Device resize, backend changes,
failures and destruction use the same idempotent output cleanup.

Regression checks assert the native swap-chain HWND is a child rather than the main
HWND, does not intercept input, and disappears after reset/QUIT. GPU tests exercise
both output paths with Reflex ON/OFF. The gameplay profiler accepts a final
`quit-check` argument and checks surface removal, Reflex OFF and a freshly rendered
browser buffer after a real play → pause → QUIT sequence. It never captures the desktop.
