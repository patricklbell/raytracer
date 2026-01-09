#pragma once

#include "raytracer_core.h"

#define RT_BACKEND_CPU 0
#define RT_BACKEND RT_BACKEND_CPU

#if RT_BACKEND == RT_BACKEND_CPU
    #include "cpu/raytracer_cpu.h"
#else
    #error Raytracer backend not supported.
#endif