#pragma once

#define rt_hook internal

typedef union RT_Handle RT_Handle;
union RT_Handle {
    u64 v64[1];
    u32 v32[2];
};
bool rt_is_zero_handle(RT_Handle handle);

// ============================================================================
// world
// ============================================================================

typedef enum RT_MaterialType {
    RT_MaterialType_Lambertian,
    RT_MaterialType_Normal,
    RT_MaterialType_Count ENUM_CASE_UNUSED,
} RT_MaterialType;

typedef struct RT_Material RT_Material;
struct RT_Material {
    RT_MaterialType type;
    vec3_f32 albedo;
};

typedef struct RT_MaterialNode RT_MaterialNode;
struct RT_MaterialNode {
    RT_Material v;
    RT_MaterialNode* next;
    RT_MaterialNode* prev;
};

typedef struct RT_MaterialList RT_MaterialList;
struct RT_MaterialList {
    RT_MaterialNode* first;
    RT_MaterialNode* last;
    u32 length;
};

typedef struct RT_Sphere RT_Sphere;
struct RT_Sphere {
    vec3_f32 center;
    f32 radius;
};

typedef enum RT_EntityType {
    RT_EntityType_Sphere,
    RT_EntityType_Count ENUM_CASE_UNUSED,
} RT_EntityType;

typedef struct RT_Entity RT_Entity;
struct RT_Entity {
    RT_EntityType type;
    RT_Handle material;
    union {
        RT_Sphere sphere;
    };
};

typedef struct RT_EntityNode RT_EntityNode;
struct RT_EntityNode {
    RT_Entity v;
    RT_EntityNode* next;
    RT_EntityNode* prev;
};

typedef struct RT_EntityList RT_EntityList;
struct RT_EntityList {
    RT_EntityNode* first;
    RT_EntityNode* last;
    u32 length;
};

typedef struct RT_World RT_World;
struct RT_World {
    Arena* arena;
    RT_EntityList entities;
    RT_MaterialList materials;
};

typedef struct RT_WorldSettings RT_WorldSettings;
struct RT_WorldSettings {};

RT_World*     rt_make_world(RT_WorldSettings settings);
void          rt_world_cleanup(RT_World* world);

RT_Handle     rt_world_add_entity(RT_World* world);
RT_Entity*    rt_world_resolve_entity(RT_World* world, RT_Handle handle);
void          rt_world_remove_entity(RT_World* world, RT_Handle handle);

RT_Handle     rt_world_add_material(RT_World* world);
RT_Material*  rt_world_resolve_material(RT_World* world, RT_Handle handle);
void          rt_world_remove_material(RT_World* world, RT_Handle handle);

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
    u8 samples;
};

rt_hook RT_Handle rt_make_tracer(RT_TracerSettings settings);
rt_hook void      rt_tracer_update_world(RT_Handle handle, RT_World* world);
rt_hook void      rt_tracer_cleanup(RT_Handle handle);
rt_hook void      rt_tracer_cast(RT_Handle tracer, RT_CastSettings settings, vec3_f32* out_color, int width, int height);
