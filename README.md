# V-Sync Simulator

This is a simulator to help game developers (and other graphics developers where latency and performance matters, such as VR) and end users understand the effects of `buffer_count` and `swapchain_count` on different scenarios by providing an interactive interface.

These 2 settings are common to most engines based on modern APIs (Vulkan, D3D12, Metal).

- `swapchain_count` is the number of swapchains requested to the API. A value of 3 is traditionally known as triple buffer (1 front buffer + 2 back buffers).
- `buffer_count` is the number of frames the CPU can get ahead of the GPU before it needs to stall. Sometimes this is called "Render Ahead".
   - Note that value has little or nothing to do with "Render Ahead vs Triple Buffer" described in an old [Anandtech article](https://www.anandtech.com/show/2794).
   - What Andandtech article describes as "Triple Buffer" can be can be achieved by setting `buffer_count >= 1`, `swapchain_count >= 3` and V-Sync must be in MAILBOX mode (Vulkan nomenclature).
   - What Andandtech article describes as "Render Ahead Queue" can be can be achieved by setting `buffer_count >= 1`, `swapchain_count >= 3` and V-Sync must be in FIFO mode (Vulkan nomenclature).

This simulator simulates worst-case latency when presenting assuming that:

 - The CPU takes cpu_time +/- cpu_time_variance to prepare the commands. i.e. Randomized in range `[cpu_time - cpu_time_variance; cpu_time + cpu_time_variance]`
 - The GPU takes gpu_time +/- gpu_time_variance to render (randomized)
 - VSync is Enabled and in FIFO mode

The code is in C++ but there is a web interface through emscripten.

[Try the simulator now](https://darksylinc.github.io/vsync_simulator/).