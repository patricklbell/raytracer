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
typedef struct RT_World RT_World;

typedef struct RT_SphereInstance RT_SphereInstance;
struct RT_SphereInstance {
    vec3_f32 center;
    f32 radius;
};

typedef struct RT_MeshInstance RT_MeshInstance;
struct RT_MeshInstance {
    RT_Handle handle;
    vec3_f32 translation;
    vec4_f32 rotation;
    vec3_f32 scale;
};

typedef enum RT_InstanceType {
    RT_InstanceType_Sphere,
    RT_InstanceType_Mesh,
    RT_InstanceType_Count ENUM_CASE_UNUSED,
} RT_InstanceType;

typedef struct RT_Instance RT_Instance;
struct RT_Instance {
    RT_InstanceType type;
    RT_Handle material;
    union {
        RT_SphereInstance sphere;
        RT_MeshInstance mesh;
    };
};

typedef struct RT_InstanceNode RT_InstanceNode;
struct RT_InstanceNode {
    RT_Instance v;
    RT_InstanceNode* next;
    RT_InstanceNode* prev;
};

typedef struct RT_InstanceList RT_InstanceList;
struct RT_InstanceList {
    RT_InstanceNode* first;
    RT_InstanceNode* last;
    u32 length;
};

RT_Handle     rt_world_add_instance(RT_World* world);
RT_Instance*  rt_world_resolve_instance(RT_World* world, RT_Handle handle);
void          rt_world_remove_instance(RT_World* world, RT_Handle handle);

typedef enum RT_MaterialType {
    RT_MaterialType_Lambertian,
    RT_MaterialType_Dieletric,
    RT_MaterialType_Metal,
    RT_MaterialType_Normal,
    RT_MaterialType_Light,
    RT_MaterialType_Count ENUM_CASE_UNUSED,
} RT_MaterialType;

typedef struct RT_Material RT_Material;
struct RT_Material {
    RT_MaterialType type;
    vec3_f32 albedo;
    vec3_f32 emissive;
    f32 roughness;
    f32 ior;

    // makes normal always point towards ray (invalid for dieletrics)
    bool billboard;
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

RT_Handle     rt_world_add_material(RT_World* world);
RT_Material*  rt_world_resolve_material(RT_World* world, RT_Handle handle);
void          rt_world_remove_material(RT_World* world, RT_Handle handle);

typedef struct RT_Mesh RT_Mesh;
struct RT_Mesh {
    const void* vertices;
    u32 vertices_count;
    const u32* indices;
    u32 indices_count;
    GEO_Primitive primitive;
    GEO_VertexAttributes attrs;

    // this breaks the abstraction but avoids a bunch of unnecessary work
    u64 blas_id;
};

typedef struct RT_MeshNode RT_MeshNode;
struct RT_MeshNode {
    RT_Mesh v;
    RT_MeshNode* next;
    RT_MeshNode* prev;
};

typedef struct RT_MeshList RT_MeshList;
struct RT_MeshList {
    RT_MeshNode* first;
    RT_MeshNode* last;
    u32 length;
};

RT_Handle rt_world_add_mesh(RT_World* world);
RT_Mesh*  rt_world_resolve_mesh(RT_World* world, RT_Handle handle);
void      rt_world_remove_mesh(RT_World* world, RT_Handle handle);

struct RT_World {
    Arena* arena;
    RT_InstanceList instances;
    RT_MaterialList materials;
    RT_MeshList meshes;
};

typedef struct RT_WorldSettings RT_WorldSettings;
struct RT_WorldSettings {};

RT_World* rt_make_world(RT_WorldSettings settings);
void      rt_world_cleanup(RT_World* world);

// ============================================================================
// tracer
// ============================================================================
struct RT_TracerSettings {
    u8 max_bounces;
    GEO_WindingOrder winding_order;
    bool sky;
};

#define RT_MAX_MAX_BOUNCES 64

typedef struct RT_CastSettings RT_CastSettings;
struct RT_CastSettings {
    vec3_f32 eye;
    vec3_f32 up;
    vec3_f32 forward;
    vec3_f32 right;
    vec3_f32 viewport;

    u8 samples;
    f32 ior;

    bool defocus;
    vec2_f32 defocus_disk;
    
    bool orthographic;
};

rt_hook RT_Handle rt_make_tracer(RT_TracerSettings settings);
rt_hook void      rt_tracer_build_blas(RT_Handle handle, RT_World* world);
rt_hook void      rt_tracer_build_tlas(RT_Handle handle, RT_World* world);
rt_hook void      rt_tracer_cleanup(RT_Handle handle);
rt_hook void      rt_tracer_cast(RT_Handle tracer, RT_CastSettings settings, vec3_f32* out_radiance, int width, int height);
