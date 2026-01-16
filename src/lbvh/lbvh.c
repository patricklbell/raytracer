RSFORCEINLINE int lbvh_morton_value_is_before(void* elementa, void* elementb) {
    return ((LBVH_MortonValue*)elementa)->morton_code < ((LBVH_MortonValue*)elementb)->morton_code;
}

// @note assumes morton codes are sorted smallest to largest
static u64 lbvh_get_split_idx(const LBVH_MortonValue* in_morton_values, u64 count) {
    Assert(count > 1);

    const u64 f = in_morton_values[0].morton_code;
    const u64 l = in_morton_values[count-1].morton_code;

    // find most significant bit that differs
    if (f == l) {
        return count/2;
    }
    const u64 split_bit_idx = 63 - count_leading_zeros_u64(f ^ l);
    const u64 split_bit = (1ull << split_bit_idx);

    // binary search for split point
    u64 lo = 1;
    u64 hi = count;
    while (lo < hi) {
        u64 mid = (lo + hi)/2;

        if (in_morton_values[mid].morton_code & split_bit) {
            hi = mid;
        } else {
            lo = mid + 1;
        }
    }
    Assert(lo > 0 && lo < count);
    return lo;
}

static LBVH_Node* lbvh_create_subtree(Arena* arena, const LBVH_MortonValue* in_morton_values, u64 count) {
    LBVH_Node* node = push_array(arena, LBVH_Node, 1);
    
    Assert(count != 0);
    if (count == 1) {
        node->value = in_morton_values[0].value;
        node->aabb = in_morton_values[0].aabb;
    } else {
        u64 m = lbvh_get_split_idx(in_morton_values, count);
        node->left = lbvh_create_subtree(arena, in_morton_values, m);
        node->right = lbvh_create_subtree(arena, in_morton_values + m, count - m);
        node->aabb = merge_rng3_f32(node->left->aabb, node->right->aabb);
    }

    return node;
}

inline LBVH_Tree* lbvh_make(Arena* arena, LBVH_MortonValue* inout_morton_values, u64 count) {
    Assert(count > 0);

    LBVH_Tree* lbvh = push_array(arena, LBVH_Tree, 1);

    radsort(inout_morton_values, count, lbvh_morton_value_is_before);
    lbvh->root = lbvh_create_subtree(arena, inout_morton_values, count);

    return lbvh;
}

static bool lbvh_aabb_query_ray(rng3_f32 aabb, const rng3_f32* in_ray, rng_f32* inout_t_interval) {
    rng_f32 overlap = *inout_t_interval;

    for (int axis = 0; axis < 3; axis++) {
        const f32 adinv = 1.f/in_ray->direction.v[axis];

        f32 t0 = (aabb.min.v[axis] - in_ray->origin.v[axis])*adinv;
        f32 t1 = (aabb.max.v[axis] - in_ray->origin.v[axis])*adinv;

        if (t0 < t1) {
            if (t0 > overlap.min) overlap.min = t0;
            if (t1 < overlap.max) overlap.max = t1;
        } else {
            if (t1 > overlap.min) overlap.min = t1;
            if (t0 < overlap.max) overlap.max = t0;
        }

        if (overlap.max <= overlap.min)
            return false;
    }

    return true;
}

static void* lbvh_node_query_ray(LBVH_Node* node, const rng3_f32* in_ray, rng_f32* inout_t_interval, LBVH_RayHitFunction hit_function, void* data) {
    if (!lbvh_aabb_query_ray(node->aabb, in_ray, inout_t_interval))
        return NULL;
    if (node->value != NULL && hit_function(node->value, in_ray, inout_t_interval, data))
        return node->value;
    
    void* left_value  = (node->left  == NULL) ? NULL : lbvh_node_query_ray(node->left, in_ray, inout_t_interval, hit_function, data);
    void* right_value = (node->right == NULL) ? NULL : lbvh_node_query_ray(node->right, in_ray, inout_t_interval, hit_function, data);

    return (right_value != NULL) ? right_value : left_value;
}

internal void* lbvh_query_ray(LBVH_Tree* lbvh, const rng3_f32* in_ray, rng_f32* inout_t_interval, LBVH_RayHitFunction hit_function, void* data) {
    return lbvh_node_query_ray(lbvh->root, in_ray, inout_t_interval, hit_function, data);
}