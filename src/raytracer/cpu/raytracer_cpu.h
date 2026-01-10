#pragma once

typedef struct RT_CPU_Ray RT_CPU_Ray;
struct RT_CPU_Ray {
    vec3_f32 origin;
    vec3_f32 direction;
};

typedef struct RT_CPU_Interval RT_CPU_Interval;
struct RT_CPU_Interval {
    f32 min;
    f32 max;
};

typedef struct RT_CPU_HitRecord RT_CPU_HitRecord;
struct RT_CPU_HitRecord {
    vec3_f32 p;
    vec3_f32 n;
    f32 t;

    RT_Handle material;
};

typedef struct RT_CPU_Tracer RT_CPU_Tracer;
struct RT_CPU_Tracer {
    Arena* arena;
    RT_World* world;
    u8 max_bounces;
};

typedef struct RT_CPU_TraceContext RT_CPU_TraceContext;
struct RT_CPU_TraceContext {
    u8 ior_count;
    f32 ior[RT_MAX_MAX_BOUNCES];
};

RT_CPU_Tracer* rt_cpu_handle_to_tracer(RT_Handle handle);
RT_Handle      rt_cpu_tracer_to_handle(RT_CPU_Tracer* tracer);

// ============================================================================
// cpu kernels
// ============================================================================
void        rt_cpu_raygen(RT_CPU_Tracer* tracer, const RT_CastSettings* settings, vec3_f32* out_radiance, int width, int height);
vec3_f32    rt_cpu_trace_ray(RT_CPU_Tracer* tracer, RT_CPU_TraceContext* ctx, const RT_CPU_Ray* in_ray, u8 depth, RT_CPU_Interval interval, RT_CPU_HitRecord* out_record);
vec3_f32    rt_cpu_closest_hit(RT_CPU_Tracer* tracer, RT_CPU_TraceContext* ctx, const RT_CPU_Ray* in_ray, u8 depth, RT_CPU_HitRecord* in_record);
vec3_f32    rt_cpu_miss(RT_CPU_Tracer* tracer, RT_CPU_TraceContext* ctx, const RT_CPU_Ray* in_ray, u8 depth);

// ============================================================================
// helpers
// ============================================================================
bool rt_cpu_intersect(RT_CPU_Tracer* tracer, const RT_CPU_Ray* in_ray, RT_CPU_Interval interval, RT_CPU_HitRecord* out_record);
bool rt_cpu_intersect_sphere(RT_CPU_Tracer* tracer, const RT_Sphere* in_sphere, const RT_CPU_Ray* in_ray, RT_CPU_Interval interval, RT_CPU_HitRecord* out_record);

RT_CPU_Interval rt_cpu_make_pos_interval();
bool rt_cpu_in_interval(f32 x, const RT_CPU_Interval* in_interval);

vec3_f32 rt_cpu_cosine_sample(vec3_f32 normal);
f32 rt_cpu_fresnel_schlick(f32 eta_i, f32 eta_t, f32 cos_theta);
vec3_f32 rt_cpu_normal_to_radiance(vec3_f32 normal);