#pragma once

typedef struct LBVH_Node LBVH_Node;
struct LBVH_Node {
    rng3_f32 aabb;
    u32 id;
    LBVH_Node* left;
    LBVH_Node* right;
};

typedef struct LBVH_Tree LBVH_Tree;
struct LBVH_Tree {
    LBVH_Node* root;
};

typedef bool (*LBVH_RayHitFunction)(u32 id, rng_f32* inout_t_interval, void* data);

internal LBVH_Tree lbvh_make(Arena* arena, rng3_f32* in_aabbs, u32 count);

internal u32 lbvh_query_ray(const LBVH_Tree* lbvh, const vec3_f32* in_ray_origin, const vec3_f32* in_ray_inv_dir, rng_f32* inout_t_interval, LBVH_RayHitFunction hit_function, void* data);

#ifdef BUILD_DEBUG
    #include "extra/dump.h"
#endif