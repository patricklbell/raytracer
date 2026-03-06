#include "lbvh/lbvh.h"
#include "common/common_arena.h"
#include "common/common_math.h"
#include "geo/geo.h"
#include "third_party/tracy/public/tracy/Tracy.hpp"
#ifdef BUILD_DEBUG
    #include "extra/dump.c"
#endif

struct NodeIndex {
    u64 morton_code;
    u32 id;
};

RSFORCEINLINE int lbvh_morton_code_is_before(void* elementa, void* elementb) {
    return ((NodeIndex*)elementa)->morton_code < ((NodeIndex*)elementb)->morton_code;
}

// @note assumes morton codes are sorted smallest to largest
static u32 lbvh_get_split_idx(const NodeIndex* in_node_index, u32 count) {
    Assert(count > 1);

    const u64 f = in_node_index[0].morton_code;
    const u64 l = in_node_index[count-1].morton_code;

    // find most significant bit that differs
    if (f == l) {
        return count/2;
    }
    const u32 split_bit_idx = 63 - count_leading_zeros_u64(f ^ l);
    const u64 split_bit = (1ull << split_bit_idx);

    // binary search for split point
    u32 lo = 0;
    u32 hi = count;
    while (lo < hi) {
        u32 mid = (lo + hi)/2;

        if (in_node_index[mid].morton_code & split_bit) {
            hi = mid;
        } else {
            lo = mid + 1;
        }
    }
    Assert(lo > 0 && lo < count);
    return lo;
}

static LBVH_Node* lbvh_create_subtree(
    Arena* arena, const NodeIndex* in_node_index, const rng3_f32* in_aabbs, u32 start, u32 count
    BUILD_DEBUG_X(, vec3_f32 min, vec3_f32 max)
) {
    LBVH_Node* node = push_array(arena, LBVH_Node, 1);
    
    Assert(count > 0);
    if (count == 1) {
        node->id = in_node_index[start].id;
        node->aabb = in_aabbs[node->id - 1];
    } else {
        u64 m = lbvh_get_split_idx(in_node_index + start, count);
        node->left = lbvh_create_subtree(arena, in_node_index, in_aabbs, start, m BUILD_DEBUG_X(, min, max));
        node->right = lbvh_create_subtree(arena, in_node_index, in_aabbs, start + m, count - m BUILD_DEBUG_X(, min, max));
        node->aabb = merge_rng3_f32(node->left->aabb, node->right->aabb);
    }

    #if BUILD_DEBUG
    Assert(all_3b(leq_3f32(node->aabb.min, node->aabb.max)));
    Assert(all_3b(geq_3f32(node->aabb.min, min)));
    Assert(all_3b(leq_3f32(node->aabb.max, max)));
    Assert(!any_3b(is_nan_3f32(node->aabb.min)));
    Assert(!any_3b(is_inf_3f32(node->aabb.min)));
    Assert(!any_3b(is_nan_3f32(node->aabb.max)));
    Assert(!any_3b(is_inf_3f32(node->aabb.max)));
    #endif

    return node;
}

static u64 lbvh_c_to_morton_code(vec3_f32 c, vec3_f32 min, vec3_f32 extents) {
    const u64 bits = sizeof(u64)*8 / 3;
    vec3_f32 norm = eldiv_3f32(sub_3f32(c, min), extents);

    u32 x = (u32)((f64)norm.x*(f64)(1 << bits));
    u32 y = (u32)((f64)norm.y*(f64)(1 << bits));
    u32 z = (u32)((f64)norm.z*(f64)(1 << bits));

    u64 morton_code = 0;
    for EachIndexU32(idx, bits) {
        morton_code |= ((x >> idx) & 1ull) << (idx*3ull + 0ull);
        morton_code |= ((y >> idx) & 1ull) << (idx*3ull + 1ull);
        morton_code |= ((z >> idx) & 1ull) << (idx*3ull + 2ull);
    }

    return morton_code;
}

internal LBVH_Tree lbvh_make(Arena* arena, rng3_f32* in_aabbs, u32 count) {ZoneScoped;    
    // determine min and extents of centers
    Assert(count > 0);
    vec3_f32 min = make_scale_3f32(MAX_F32), max = make_scale_3f32(-MAX_F32);
    for (u64 idx = 0; idx < count; idx++) {
        #if !BUILD_DEBUG
        vec3_f32 c = mul_3f32(add_3f32(in_aabbs[idx].min, in_aabbs[idx].max), 0.5f);
        min = min_3f32(min, c);
        max = max_3f32(max, c);
        #else
        min = min_3f32(min, in_aabbs[idx].min);
        max = max_3f32(max, in_aabbs[idx].max);
        #endif
    }
    vec3_f32 extents = sub_3f32(max, min);

    LBVH_Tree result;
    {DeferResource(Temp scratch = scratch_begin(NULL, 0), scratch_end(scratch)) {
        NodeIndex* node_index = push_array_no_zero(scratch.arena, NodeIndex, count);

        // calculate morton codes and set ids
        for (u64 idx = 0; idx < count; idx++) {
            vec3_f32 c = mul_3f32(add_3f32(in_aabbs[idx].min, in_aabbs[idx].max), 0.5f);
            node_index[idx].morton_code = lbvh_c_to_morton_code(c, min, extents);
            node_index[idx].id = idx + 1;
        }

        // build
        {
            ZoneScopedN("sort");
            radsort(node_index, count, lbvh_morton_code_is_before);
        }
        {
            ZoneScopedN("tree");
            result.root = lbvh_create_subtree(arena, node_index, in_aabbs, 0, count BUILD_DEBUG_X(, min, max));
        }
    }}

    return result;
}

// internal LBVH_TreeFlat lbvh_make_flat(Arena* arena, rng3_f32* in_aabbs, u32 count) {
//     LBVH_TreeFlat result;
//     result.count = count;
//     result.nodes = push_array(arena, LBVH_NodeFlat, result.count);
//     result.root = 0;

    
// }

static bool lbvh_aabb_query_ray(rng3_f32 aabb, const vec3_f32* in_ray_origin, const vec3_f32* in_ray_inv_dir, rng_f32* inout_t_interval) {ZoneScoped;
    vec3_f32 t0 = elmul_3f32(sub_3f32(aabb.min, *in_ray_origin), *in_ray_inv_dir);
    vec3_f32 t1 = elmul_3f32(sub_3f32(aabb.max, *in_ray_origin), *in_ray_inv_dir);

    vec3_b neg = is_neg_3f32(*in_ray_inv_dir);
    vec3_f32 t_min = {
        .x = !neg.x ? t0.x : t1.x,
        .y = !neg.y ? t0.y : t1.y,
        .z = !neg.z ? t0.z : t1.z,
    };
    vec3_f32 t_max = {
        .x = !neg.x ? t1.x : t0.x,
        .y = !neg.y ? t1.y : t0.y,
        .z = !neg.z ? t1.z : t0.z,
    };
    
    rng_f32 overlap = {
        .min = Max(Max(t_min.x, t_min.y), t_min.z),
        .max = Min(Min(t_max.x, t_max.y), t_max.z),
    };
    
    return overlap.min <= overlap.max && geo_in_interval(overlap.min, inout_t_interval);
}

static u32 lbvh_node_query_ray(const LBVH_Node* node, const vec3_f32* in_ray_origin, const vec3_f32* in_ray_inv_dir, rng_f32* inout_t_interval, LBVH_RayHitFunction hit_function, void* data) {ZoneScoped;
    if (!lbvh_aabb_query_ray(node->aabb, in_ray_origin, in_ray_inv_dir, inout_t_interval))
        return 0;
    if (node->id > 0 && hit_function(node->id, inout_t_interval, data))
        return node->id;
    
    u32 left_id  = (node->left  == NULL) ? 0 : lbvh_node_query_ray(node->left, in_ray_origin, in_ray_inv_dir, inout_t_interval, hit_function, data);
    u32 right_id = (node->right == NULL) ? 0 : lbvh_node_query_ray(node->right, in_ray_origin, in_ray_inv_dir, inout_t_interval, hit_function, data);

    return (right_id > 0) ? right_id : left_id;
}

internal u32 lbvh_query_ray(const LBVH_Tree* lbvh, const vec3_f32* in_ray_origin, const vec3_f32* in_ray_inv_dir, rng_f32* inout_t_interval, LBVH_RayHitFunction hit_function, void* data) {
    return lbvh_node_query_ray(lbvh->root, in_ray_origin, in_ray_inv_dir, inout_t_interval, hit_function, data);
}