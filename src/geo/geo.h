#pragma once

// vertex
typedef enum GEO_VertexAttributes {
    GEO_VertexAttributes_ZERO = 0,
    GEO_VertexAttributes_P    = 1 << 0,
    GEO_VertexAttributes_N    = 1 << 1,
    GEO_VertexAttributes_T    = 1 << 2,
    GEO_VertexAttributes_C    = 1 << 3,
    GEO_VertexAttributes_PN   = GEO_VertexAttributes_P   | GEO_VertexAttributes_N,
    GEO_VertexAttributes_PT   = GEO_VertexAttributes_P   | GEO_VertexAttributes_T,
    GEO_VertexAttributes_PC   = GEO_VertexAttributes_P   | GEO_VertexAttributes_C,
    GEO_VertexAttributes_NT   = GEO_VertexAttributes_N   | GEO_VertexAttributes_T,
    GEO_VertexAttributes_NC   = GEO_VertexAttributes_N   | GEO_VertexAttributes_C,
    GEO_VertexAttributes_TC   = GEO_VertexAttributes_T   | GEO_VertexAttributes_C,
    GEO_VertexAttributes_PTC  = GEO_VertexAttributes_PT  | GEO_VertexAttributes_C,
    GEO_VertexAttributes_PNC  = GEO_VertexAttributes_PN  | GEO_VertexAttributes_C,
    GEO_VertexAttributes_PNT  = GEO_VertexAttributes_PN  | GEO_VertexAttributes_T,
    GEO_VertexAttributes_NTC  = GEO_VertexAttributes_NT  | GEO_VertexAttributes_C,
    GEO_VertexAttributes_PNTC = GEO_VertexAttributes_PNT | GEO_VertexAttributes_C,
    GEO_VertexAttributes_COUNT,
} GEO_VertexAttributes;

typedef vec3_f32 GEO_VertexType_P;
typedef vec3_f32 GEO_VertexType_N;
typedef vec2_f32 GEO_VertexType_T;
typedef vec3_f32 GEO_VertexType_C;

internal force_inline u64 geo_vertex_size(GEO_VertexAttributes attrs);
internal force_inline u64 geo_vertex_align(GEO_VertexAttributes attrs);
internal force_inline u64 geo_vertex_offset(GEO_VertexAttributes attrs, GEO_VertexAttributes attr);
internal force_inline u64 geo_vertex_stride(GEO_VertexAttributes attrs, GEO_VertexAttributes attr);
internal force_inline u64 geo_vertex_i_offset(GEO_VertexAttributes attrs, GEO_VertexAttributes attr, u64 i);

typedef enum GEO_Primitive {
    GEO_Primitive_ZERO = 0,
    GEO_Primitive_POINT_LIST,
    GEO_Primitive_LINE_LIST,
    GEO_Primitive_LINE_STRIP,
    GEO_Primitive_TRI_LIST,
    GEO_Primitive_TRI_STRIP,
    GEO_Primitive_RECT_LIST,
    GEO_Primitive_COUNT ENUM_CASE_UNUSED,
} GEO_Primitive;

typedef enum GEO_WindingOrder {
    GEO_WindingOrder_CCW = 0,
    GEO_WindingOrder_CW,
} GEO_WindingOrder;

// intersection
internal rng_f32 geo_make_pos_interval();
internal bool geo_in_interval(f32 x, const rng_f32* in_interval);
internal bool geo_intersect_sphere(const rng3_f32* in_ray, vec3_f32 center, f32 radius, rng_f32* inout_interval);
internal bool geo_intersect_tri(const rng3_f32* in_ray, vec3_f32 in_tri_a, vec3_f32 in_tri_b, vec3_f32 in_tri_c, rng_f32* inout_interval);
