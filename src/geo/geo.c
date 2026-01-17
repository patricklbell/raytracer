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