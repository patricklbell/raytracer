#include "raytracer_core.c"

#if RT_BACKEND == RT_BACKEND_CPU
    #include "cpu/raytracer_cpu.c"
#else
    #error Raytracer backend not supported.
#endif