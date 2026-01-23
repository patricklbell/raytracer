// vertices
internal force_inline u64 geo_vertex_size(GEO_VertexAttributes attrs) {
    return count_ones_u64(attrs)*sizeof(vec4_f32);
}
internal force_inline u64 geo_vertex_align(GEO_VertexAttributes attrs) {
    return sizeof(vec4_f32);
}
internal force_inline u64 geo_vertex_offset(GEO_VertexAttributes attrs, GEO_VertexAttributes attr) {
    Assert(attrs & attr);
    return count_ones_u64(attrs & (attr-1))*sizeof(vec4_f32);
}
internal force_inline u64 geo_vertex_stride(GEO_VertexAttributes attrs, GEO_VertexAttributes attr) {
    Assert(attrs & attr);
    return geo_vertex_size(attrs);
}
internal force_inline u64 geo_vertex_i_offset(GEO_VertexAttributes attrs, GEO_VertexAttributes attr, u64 i) {
    return geo_vertex_offset(attrs, attr) + i*geo_vertex_stride(attrs, attr);
}

internal rng3_f32 geo_aab3_f32_merge(rng3_f32 a, rng3_f32 b) {
    return (rng3_f32){
        .min = make_3f32(Min(a.min.x, b.min.x), Min(a.min.y, b.min.y), Min(a.min.z, b.min.z)),
        .max = make_3f32(Max(a.max.x, b.max.x), Max(a.max.y, b.max.y), Max(a.max.z, b.max.z)),
    };
}

// intersections
internal rng_f32 geo_make_pos_interval() {
    return (rng_f32){EPSILON_F32, MAX_F32};
}

internal bool geo_in_interval(f32 x, const rng_f32* in_interval) {
    return x >= in_interval->min && x <= in_interval->max; 
}

internal bool geo_intersect_sphere(
    const rng3_f32* in_ray,
    vec3_f32 center, f32 radius,
    rng_f32* inout_interval
) {
    Assert(radius > 0.f);

    // Sphere: |p - c|^2 = r^2
    // Line  : p = t*d + o
    // => |t*d + o - c|^2 = r^2
    // => (t*d + o - c) . (t*d + o - c) = r^2
    // => t^2*|d|^2 + 2*t*d.(o - c) + |o - c|^2 - r^2 = 0
    // define a, b, c s.t. at^2 + bt + c = 0
    vec3_f32 o_minus_c = sub_3f32(in_ray->origin, center);
    f32 a = length2_3f32(in_ray->direction);
    f32 half_b = dot_3f32(in_ray->direction, o_minus_c);
    f32 c = length2_3f32(o_minus_c) - radius*radius;

    f32 qtr_d = half_b*half_b - a*c;
    if (qtr_d < 0.f)
        return false;
    f32 half_sqrt_d = sqrt_f32(qtr_d);

    // since a is always positive we know - is the closer solution
    f32 t = (-half_b - half_sqrt_d)/a;
    if (!geo_in_interval(t, inout_interval)) {
        t = (-half_b + half_sqrt_d)/a;
        if (!geo_in_interval(t, inout_interval))
            return false;
    }

    inout_interval->max = t;
    return true;
}

// https://en.wikipedia.org/wiki/M%C3%B6ller%E2%80%93Trumbore_intersection_algorithm
internal bool geo_intersect_tri(
    const rng3_f32* in_ray,
    vec3_f32 in_tri_a, vec3_f32 in_tri_b, vec3_f32 in_tri_c,
    rng_f32* inout_interval, vec2_f32* out_uvw
) {
    vec3_f32 edge1 = sub_3f32(in_tri_b, in_tri_a);
    vec3_f32 edge2 = sub_3f32(in_tri_c, in_tri_a);
    vec3_f32 ray_cross_e2 = cross_3f32(in_ray->direction, edge2);
    f32 det = dot_3f32(edge1, ray_cross_e2);

    if (det == 0.f)
        return false;

    f32 inv_det = 1.f / det;
    vec3_f32 s = sub_3f32(in_ray->origin, in_tri_a);
    f32 u = inv_det * dot_3f32(s, ray_cross_e2);

    if ((u < 0 && abs_f32(u) > EPSILON_F32) || (u > 1 && abs_f32(u-1) > EPSILON_F32))
        return false;

    vec3_f32 s_cross_e1 = cross_3f32(s, edge1);
    f32 v = inv_det * dot_3f32(in_ray->direction, s_cross_e1);

    if ((v < 0 && abs_f32(v) > EPSILON_F32) || (u + v > 1 && abs_f32(u + v - 1) > EPSILON_F32))
        return false;

    // At this stage we can compute t to find out where the intersection point is on the line.
    f32 t = inv_det * dot_3f32(edge2, s_cross_e1);

    if (!geo_in_interval(t, inout_interval)) {
        return false;
    }
    
    inout_interval->max = t;
    out_uvw->U = u;
    out_uvw->V = v;
    return true;
}