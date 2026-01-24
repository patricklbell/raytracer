#pragma once

typedef struct LBVH_Node LBVH_Node;
struct LBVH_Node {
    rng3_f32 aabb;
    u64 id;
    LBVH_Node* left;
    LBVH_Node* right;
};

typedef struct LBVH_Tree LBVH_Tree;
struct LBVH_Tree {
    LBVH_Node* root;
};

typedef bool (*LBVH_RayHitFunction)(u64 id, const rng3_f32* in_ray, rng_f32* inout_t_interval, void* data);

internal LBVH_Tree lbvh_make(Arena* arena, rng3_f32* in_aabbs, u64 count);
internal u64       lbvh_query_ray(const LBVH_Tree* lbvh, const rng3_f32* in_ray, rng_f32* inout_t_interval, LBVH_RayHitFunction hit_function, void* data);

#ifdef BUILD_DEBUG
    #include "extra/dump.h"
#endif