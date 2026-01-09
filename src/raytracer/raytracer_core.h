#pragma once

#define rt_hook internal

typedef union RT_Handle RT_Handle;
union RT_Handle {
    u64 v64[1];
    u32 v32[2];
};

// ============================================================================
// world
// ============================================================================
typedef struct RT_Sphere RT_Sphere;
struct RT_Sphere {
    vec3_f32 center;
    vec3_f32 color;
    f32 radius;
};

typedef enum RT_EntityType {
    RT_EntityType_Sphere,
    RT_EntityType_Count,
} RT_EntityType;

typedef struct RT_Entity RT_Entity;
struct RT_Entity {
    RT_EntityType type;
    union {
        RT_Sphere sphere;
    };
};

typedef struct RT_EntityNode RT_EntityNode;
struct RT_EntityNode {
    RT_Entity v;
    RT_EntityNode* next;
};

typedef struct RT_World RT_World;
struct RT_World {
    Arena* arena;
    RT_EntityNode* entities;
    u64 entity_count;
};

typedef struct RT_WorldSettings RT_WorldSettings;
struct RT_WorldSettings {};

RT_World*   rt_make_world(RT_WorldSettings settings);
void        rt_world_add_sphere(RT_World* world, RT_Sphere sphere);
void        rt_world_cleanup(RT_World* world);

// ============================================================================
// tracer
// ============================================================================
struct RT_TracerSettings {};

typedef struct RT_CastSettings RT_CastSettings;
struct RT_CastSettings {
    vec3_f32 eye;
    vec3_f32 up;
    vec3_f32 forward;
    vec2_f32 z_extents;
    f32 z_near;
};

rt_hook RT_Handle rt_make_tracer(RT_TracerSettings settings);
rt_hook void      rt_tracer_update_world(RT_Handle handle, RT_World* world);
rt_hook void      rt_tracer_cleanup(RT_Handle handle);
rt_hook void      rt_tracer_cast(RT_Handle tracer, RT_CastSettings settings, vec3_f32* out_color, int width, int height);
