RSFORCEINLINE int lbvh_morton_code_is_before(void* elementa, void* elementb) {
    return (u64*)elementa < (u64*)elementb;
}

// @note assumes morton codes are sorted smallest to largest
static u64 lbvh_get_split_idx(const u64* in_morton_codes, u64 count) {
    Assert(count > 1);

    const u64 f = in_morton_codes[0];
    const u64 l = in_morton_codes[count-1];

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

        if (in_morton_codes[mid] & split_bit) {
            hi = mid;
        } else {
            lo = mid + 1;
        }
    }
    Assert(lo > 0 && lo < count);
    return lo;
}

static LBVH_Node* lbvh_create_subtree(Arena* arena, const u64* in_morton_codes, const rng3_f32* in_aabbs, u64 start, u64 count) {
    LBVH_Node* node = push_array(arena, LBVH_Node, 1);
    
    Assert(count > 0);
    if (count == 1) {
        node->id = start + 1;
        node->aabb = in_aabbs[start];
    } else {
        u64 m = lbvh_get_split_idx(in_morton_codes + start, count);
        node->left = lbvh_create_subtree(arena, in_morton_codes, in_aabbs, start, m);
        node->right = lbvh_create_subtree(arena, in_morton_codes, in_aabbs, start + m, count - m);
        node->aabb = merge_rng3_f32(node->left->aabb, node->right->aabb);
    }

    return node;
}

static u64 lbvh_c_to_morton_code(vec3_f32 c, vec3_f32 min, vec3_f32 extents) {
    const u64 bits = sizeof(u64)*8 / 3;
    vec3_f32 norm = eldiv_3f32(sub_3f32(c, min), extents);

    u32 x = (u32)((f64)norm.x/(f64)(1 << bits));
    u32 y = (u32)((f64)norm.y/(f64)(1 << bits));
    u32 z = (u32)((f64)norm.z/(f64)(1 << bits));

    u64 morton_code = 0;
    for EachIndex(idx, bits) {
        morton_code |= ((x >> idx) & 1) << (idx*3 + 0);
        morton_code |= ((y >> idx) & 1) << (idx*3 + 1);
        morton_code |= ((z >> idx) & 1) << (idx*3 + 2);
    }

    return morton_code;
}

internal LBVH_Tree lbvh_make(Arena* arena, rng3_f32* in_aabbs, u64 count) {    
    // determine min and extents of centers
    Assert(count > 0);
    vec3_f32 min = make_scale_3f32(MIN_F32), max = make_scale_3f32(MAX_F32);
    for (u64 idx = 0; idx < count; idx++) {
        vec3_f32 c = mul_3f32(add_3f32(in_aabbs[idx].min, in_aabbs[idx].max), 0.5f);
        min = min_3f32(min, c);
        max = max_3f32(max, c);
    }
    vec3_f32 extents = sub_3f32(max, min);

    LBVH_Tree result;
    {DeferResource(Temp scratch = scratch_begin(NULL, 0), scratch_end(scratch)) {
        u64* morton_codes = push_array_no_zero(scratch.arena, u64, count);

        // calculate morton codes
        for (u64 idx = 0; idx < count; idx++) {
            vec3_f32 c = mul_3f32(add_3f32(in_aabbs[idx].min, in_aabbs[idx].max), 0.5f);
            morton_codes[idx] = lbvh_c_to_morton_code(c, min, extents);
        }

        // build
        radsort(morton_codes, count, lbvh_morton_code_is_before);
        result.root = lbvh_create_subtree(arena, morton_codes, in_aabbs, 0, count);
    }}

    return result;
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

        if (overlap.max < overlap.min)
            return false;
    }

    return true;
}

static u64 lbvh_node_query_ray(LBVH_Node* node, const rng3_f32* in_ray, rng_f32* inout_t_interval, LBVH_RayHitFunction hit_function, void* data) {
    if (!lbvh_aabb_query_ray(node->aabb, in_ray, inout_t_interval))
        return 0;
    if (node->id > 0 && hit_function(node->id, in_ray, inout_t_interval, data))
        return node->id;
    
    u64 left_id  = (node->left  == NULL) ? 0 : lbvh_node_query_ray(node->left, in_ray, inout_t_interval, hit_function, data);
    u64 right_id = (node->right == NULL) ? 0 : lbvh_node_query_ray(node->right, in_ray, inout_t_interval, hit_function, data);

    return (right_id > 0) ? right_id : left_id;
}

internal u64 lbvh_query_ray(LBVH_Tree* lbvh, const rng3_f32* in_ray, rng_f32* inout_t_interval, LBVH_RayHitFunction hit_function, void* data) {
    return lbvh_node_query_ray(lbvh->root, in_ray, inout_t_interval, hit_function, data);
}