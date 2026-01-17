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

// primitives
typedef enum GEO_Primitive {
    GEO_Primitive_ZERO = 0,
    GEO_Primitive_Points,
    GEO_Primitive_Lines,
    GEO_Primitive_LineStrip,
    GEO_Primitive_Triangles,
    GEO_Primitive_TriangleStrip,
    GEO_Primitive_Quads,
    GEO_Primitive_COUNT ENUM_CASE_UNUSED,
} GEO_Primitive;