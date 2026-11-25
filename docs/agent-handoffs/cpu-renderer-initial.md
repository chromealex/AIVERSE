# CPU renderer delivery

Base 54ca01d1b. First milestone: a standalone offscreen graphics device using the existing Device interface, native triangle rasterization, framebuffer capture, and a verified public caller. Subsequent integration needs documented shader coverage and application entry points. CPU-only memory belongs to the Device. No graphics driver or new external dependency. Acceptance includes explicit errors for unsupported shader stages and formats. No tests run yet.
