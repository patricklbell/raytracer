#pragma once

typedef struct LBVH_MortonValue LBVH_MortonValue;
struct LBVH_MortonValue {
    u64 morton_code;
    rng3_f32 aabb;
    void* value;
};

typedef struct LBVH_Node LBVH_Node;
struct LBVH_Node {
    rng3_f32 aabb;
    void* value;
    LBVH_Node* left;
    LBVH_Node* right;
};

typedef struct LBVH_Tree LBVH_Tree;
struct LBVH_Tree {
    LBVH_Node* root;
};

typedef bool (*LBVH_RayHitFunction)(void* value, const rng3_f32* in_ray, rng_f32* inout_t_interval, void* data);

internal LBVH_Tree* lbvh_make(Arena* arena, LBVH_MortonValue* inout_morton_values, u64 count);
internal void*      lbvh_query_ray(LBVH_Tree* lbvh, const rng3_f32* in_ray, rng_f32* inout_t_interval, LBVH_RayHitFunction hit_function, void* data);

#ifdef BUILD_DEBUG
    #include "extra/dump.h"
#endif