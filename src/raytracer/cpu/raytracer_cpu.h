#pragma once

struct RT_CPU_Ray {
    vec3_f32 origin;
    vec3_f32 direction;
};

struct RT_CPU_Interval {
    f32 min;
    f32 max;
};

struct RT_CPU_HitRecord {
    vec3_f32 p;
    vec3_f32 n;
    f32 t;

    RT_Handle material;
};

struct RT_CPU_Tracer {
    Arena* arena;
    RT_World* world;
};

RT_CPU_Tracer*   rt_cpu_handle_to_tracer(RT_Handle handle);
RT_Handle       rt_cpu_tracer_to_handle(RT_CPU_Tracer* tracer);

// ============================================================================
// cpu kernels
// ============================================================================
void        rt_cpu_raygen(RT_CPU_Tracer* tracer, const RT_CastSettings* settings, vec3_f32* out_color, int width, int height);
bool        rt_cpu_trace_ray(RT_CPU_Tracer* tracer, const RT_CPU_Ray* in_ray, RT_CPU_Interval interval, RT_CPU_HitRecord* out_record);
vec3_f32    rt_cpu_closest_hit(RT_CPU_Tracer* tracer, const RT_CPU_Ray* ray, RT_CPU_HitRecord* in_record);
vec3_f32    rt_cpu_miss(RT_CPU_Tracer* tracer, const RT_CPU_Ray* ray);

// ============================================================================
// helpers
// ============================================================================
bool rt_cpu_trace_ray_sphere(RT_CPU_Tracer* tracer, const RT_Sphere* in_sphere, const RT_CPU_Ray* in_ray, RT_CPU_Interval interval, RT_CPU_HitRecord* out_record);

bool rt_cpu_in_interval(f32 x, const RT_CPU_Interval* in_interval);