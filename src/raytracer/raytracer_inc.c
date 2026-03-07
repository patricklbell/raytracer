#include "raytracer_core.c"

#if RT_BACKEND == RT_BACKEND_CPU
    #include "cpu/raytracer_cpu.c"
#elif RT_BACKEND == RT_BACKEND_VULKAN
    #include "vulkan/raytracer_vulkan.c"
#else
    #error Raytracer backend not supported.
#endif