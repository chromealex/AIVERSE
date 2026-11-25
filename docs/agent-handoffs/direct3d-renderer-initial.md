# Native Direct3D delivery

Start from a715cae1e. Share one Windows-facing device contract. First stage creates real D3D11 and D3D9 devices, renders into owned offscreen surfaces and reads GPU memory for pixel verification. Preserve existing default platform choices. Confirm native Windows execution before claiming acceptance. Hardware, shader portability, normal window presentation and device loss are acceptance requirements for client integration. No tests run yet.
