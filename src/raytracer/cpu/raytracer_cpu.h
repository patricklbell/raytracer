#pragma once

typedef struct RT_CPU_HitRecord RT_CPU_HitRecord;
struct RT_CPU_HitRecord {
    vec3_f32 p;
    vec3_f32 n;
    f32 t;

    RT_Handle material;
};

typedef struct RT_CPU_TLAS RT_CPU_TLAS;
struct RT_CPU_TLAS {
    LBVH_Tree lbvh;
};

typedef struct RT_CPU_BLASNode RT_CPU_BLASNode;
struct RT_CPU_BLASNode {
    LBVH_Tree* lbvh;
    RT_Entity* entity;
};

typedef struct RT_CPU_BLAS RT_CPU_BLAS;
struct RT_CPU_BLAS {
    RT_CPU_BLASNode* nodes;
    u64 node_count;
};

typedef struct RT_CPU_Tracer RT_CPU_Tracer;
struct RT_CPU_Tracer {
    Arena* arena;
    u8 max_bounces;

    Arena* tlas_arena;
    RT_CPU_TLAS tlas;

    Arena* blas_arena;
    RT_CPU_BLAS blas;
};

typedef struct RT_CPU_TraceContext RT_CPU_TraceContext;
struct RT_CPU_TraceContext {
    u8 ior_count;
    f32 ior[RT_MAX_MAX_BOUNCES];
};

internal RT_CPU_Tracer* rt_cpu_handle_to_tracer(RT_Handle handle);
internal RT_Handle      rt_cpu_tracer_to_handle(RT_CPU_Tracer* tracer);

// ============================================================================
// acceleration structures
// ============================================================================

internal void rt_cpu_build_blas(RT_CPU_BLAS* out_blas, Arena* arena, RT_World* world);
internal void rt_cpu_build_tlas(RT_CPU_TLAS* out_tlas, Arena* arena, const RT_CPU_BLAS* in_blas);

// ============================================================================
// cpu kernels
// ============================================================================
internal void     rt_cpu_raygen(RT_CPU_Tracer* tracer, const RT_CastSettings* settings, vec3_f32* out_radiance, int width, int height);
internal vec3_f32 rt_cpu_trace_ray(RT_CPU_Tracer* tracer, RT_CPU_TraceContext* ctx, const rng3_f32* in_ray, u8 depth, rng_f32 interval, RT_CPU_HitRecord* out_record);
internal vec3_f32 rt_cpu_closest_hit(RT_CPU_Tracer* tracer, RT_CPU_TraceContext* ctx, const rng3_f32* in_ray, u8 depth, RT_CPU_HitRecord* in_record);
internal vec3_f32 rt_cpu_miss(RT_CPU_Tracer* tracer, RT_CPU_TraceContext* ctx, const rng3_f32* in_ray, u8 depth);

// ============================================================================
// intersection
// ============================================================================
internal bool rt_cpu_intersect(RT_CPU_Tracer* tracer, const rng3_f32* in_ray, rng_f32 interval, RT_CPU_HitRecord* out_record);
internal bool rt_cpu_intersect_sphere(RT_CPU_Tracer* tracer, const RT_Sphere* in_sphere, const rng3_f32* in_ray, rng_f32 interval, RT_CPU_HitRecord* out_record);

// ============================================================================
// helpers
// ============================================================================
internal rng_f32 rt_cpu_make_pos_interval();
internal bool rt_cpu_in_interval(f32 x, const rng_f32* in_interval);

internal vec3_f32 rt_cpu_cosine_sample(vec3_f32 normal);
internal f32 rt_cpu_fresnel_schlick(f32 eta_i, f32 eta_t, f32 cos_theta);
internal vec3_f32 rt_cpu_normal_to_radiance(vec3_f32 normal);