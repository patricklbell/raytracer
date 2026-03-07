#pragma once

#include "raytracer_core.h"

#define RT_BACKEND_CPU 0
#define RT_BACKEND_VULKAN 1

#if defined(VULKAN_ENABLED)
    #define RT_BACKEND RT_BACKEND_VULKAN
#else
    #define RT_BACKEND RT_BACKEND_CPU
#endif

#if RT_BACKEND == RT_BACKEND_CPU
    #include "cpu/raytracer_cpu.h"
#elif RT_BACKEND == RT_BACKEND_VULKAN
    #include "vulkan/raytracer_vulkan.h"
#else
    #error Raytracer backend not supported.
#endif