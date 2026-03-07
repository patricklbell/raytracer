#version 460
#extension GL_EXT_ray_tracing     : require
#extension GL_EXT_nonuniform_qualifier : enable

// ============================================================================
// Payload (must match rgen.glsl and rmiss.glsl)
// ============================================================================
struct RayPayload {
    vec3  radiance;
    uint  scatter;
    vec3  attenuation;
    float current_ior;
    vec3  scatter_origin;
    uint  seed;
    vec3  scatter_dir;
    float _pad0;
};

layout(location = 0) rayPayloadInEXT RayPayload payload;
hitAttributeEXT vec2 bary_coord;

// ============================================================================
// Material types (must match RT_MaterialType in raytracer_core.h)
// ============================================================================
#define MAT_LAMBERTIAN 0u
#define MAT_DIELECTRIC 1u
#define MAT_METAL      2u
#define MAT_NORMAL     3u
#define MAT_LIGHT      4u

// ============================================================================
// GPU buffer structs
// ============================================================================

// Combined mesh vertex data (all floats, tightly packed per-vertex blocks)
layout(std430, set = 0, binding = 3) buffer VertexBuffer {
    float vtx[];
};

// Combined mesh index data (uint32 per index entry)
layout(std430, set = 0, binding = 4) buffer IndexBuffer {
    uint idx[];
};

// Per-mesh geometry metadata
struct MeshInfo {
    uint vtx_float_offset;      // float index of this mesh's first vertex in vtx[]
    uint idx_elem_offset;       // element index of this mesh's first index in idx[]
    uint vertex_stride_floats;  // floats between consecutive vertices
    uint normal_float_offset;   // float offset from vertex start to its normal (0 if no normal)
    uint has_normal;            // 1 if the mesh has per-vertex normals
    uint auto_index;            // 1 if tris are sequential (no index buffer)
    uint tri_count;
    uint vertex_count;
};
layout(std430, set = 0, binding = 5) buffer MeshInfoBuffer {
    MeshInfo meshes[];
};

// Per-TLAS-instance metadata (written by rt_tracer_build_tlas)
struct InstanceMeta {
    uint mesh_index;
    uint material_index;
    uint _pad0;
    uint _pad1;
};
layout(std430, set = 0, binding = 6) buffer InstanceMetaBuffer {
    InstanceMeta instance_meta[];
};

// Material data
struct GPUMaterial {
    vec4  albedo;
    vec4  emissive;
    uint  type;
    float roughness;
    float ior;
    uint  billboard;  // 1 = always face camera (flip normal if back-facing)
};
layout(std430, set = 0, binding = 7) buffer MaterialBuffer {
    GPUMaterial materials[];
};

// ============================================================================
// RNG helpers (same as rgen.glsl)
// ============================================================================
uint hash_u32(uint x) {
    x += (x << 10u); x ^= (x >>  6u);
    x += (x <<  3u); x ^= (x >> 11u);
    x += (x << 15u);
    return x;
}
float rand_f(inout uint s) {
    s = hash_u32(s);
    return uintBitsToFloat((s >> 9u) | 0x3f800000u) - 1.0;
}

// Random vector in the unit sphere (rejection)
vec3 rand_unit_sphere(inout uint s) {
    vec3 p;
    int limit = 64;
    do {
        p = vec3(rand_f(s), rand_f(s), rand_f(s)) * 2.0 - 1.0;
        limit--;
    } while (dot(p, p) >= 1.0 && limit > 0);
    return p;
}

// Cosine-weighted hemisphere sample (Malley's method)
vec3 rand_cosine_hemisphere(inout uint s, vec3 n) {
    float r1  = rand_f(s);
    float r2  = rand_f(s);
    float phi = 6.28318530718 * r1;
    float sr2 = sqrt(r2);

    // Build ONB around n
    vec3 t = (abs(n.x) > 0.9) ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 u = normalize(cross(t, n));
    vec3 v = cross(n, u);

    return normalize(sr2 * cos(phi) * u + sr2 * sin(phi) * v + sqrt(max(0.0, 1.0 - r2)) * n);
}

// ============================================================================
// Fresnel (Schlick approximation)
// ============================================================================
float fresnel_schlick(float cos_i, float r0) {
    float x = 1.0 - abs(cos_i);
    return r0 + (1.0 - r0) * (x * x * x * x * x);
}

float reflectance(float cos_i, float eta_i, float eta_t) {
    float r0 = (eta_i - eta_t) / (eta_i + eta_t);
    r0 *= r0;
    return fresnel_schlick(cos_i, r0);
}

// ============================================================================
// Vertex / index fetch helpers
// ============================================================================
vec3 fetch_position(MeshInfo m, uint vi) {
    uint base = m.vtx_float_offset + vi * m.vertex_stride_floats;
    return vec3(vtx[base], vtx[base + 1u], vtx[base + 2u]);
}

vec3 fetch_normal_attr(MeshInfo m, uint vi) {
    uint base = m.vtx_float_offset + vi * m.vertex_stride_floats + m.normal_float_offset;
    return vec3(vtx[base], vtx[base + 1u], vtx[base + 2u]);
}

void get_tri_indices(MeshInfo m, uint prim_id,
                     out uint i0, out uint i1, out uint i2) {
    if (m.auto_index != 0u) {
        i0 = prim_id * 3u + 0u;
        i1 = prim_id * 3u + 1u;
        i2 = prim_id * 3u + 2u;
    } else {
        uint base = m.idx_elem_offset + prim_id * 3u;
        i0 = idx[base + 0u];
        i1 = idx[base + 1u];
        i2 = idx[base + 2u];
    }
}

// ============================================================================
// main
// ============================================================================
void main() {
    // --------------------------------------------------------
    // Resolve hit instance → mesh / material
    // --------------------------------------------------------
    uint inst_id   = uint(gl_InstanceCustomIndexEXT);
    InstanceMeta im = instance_meta[inst_id];
    MeshInfo     mi = meshes[im.mesh_index];
    GPUMaterial  mat = materials[im.material_index];

    // --------------------------------------------------------
    // Fetch triangle vertices in object space, interpolate
    // --------------------------------------------------------
    uint i0, i1, i2;
    get_tri_indices(mi, uint(gl_PrimitiveID), i0, i1, i2);

    vec3 p0 = fetch_position(mi, i0);
    vec3 p1 = fetch_position(mi, i1);
    vec3 p2 = fetch_position(mi, i2);

    float w  = 1.0 - bary_coord.x - bary_coord.y;
    vec3 pos_obj   = w * p0 + bary_coord.x * p1 + bary_coord.y * p2;
    vec3 pos_world = vec3(gl_ObjectToWorldEXT * vec4(pos_obj, 1.0));

    // --------------------------------------------------------
    // Compute shading normal (interpolate vertex normals if available,
    // otherwise use geometric face normal)
    // --------------------------------------------------------
    mat3 obj_to_world_3x3 = mat3(gl_ObjectToWorldEXT);

    // Geometric normal from triangle edges
    vec3 edge01  = p1 - p0;
    vec3 edge02  = p2 - p0;
    vec3 geom_n  = normalize(obj_to_world_3x3 * cross(edge01, edge02));

    vec3 shading_n;
    if (mi.has_normal != 0u) {
        vec3 n0 = fetch_normal_attr(mi, i0);
        vec3 n1 = fetch_normal_attr(mi, i1);
        vec3 n2 = fetch_normal_attr(mi, i2);
        vec3 interp_n = w * n0 + bary_coord.x * n1 + bary_coord.y * n2;
        float nlen = length(interp_n);
        shading_n = (nlen > 1e-5) ? normalize(obj_to_world_3x3 * interp_n) : geom_n;
    } else {
        shading_n = geom_n;
    }

    // Billboard: flip normal so it always faces the incoming ray
    // (same semantics as the CPU closest-hit)
    if (mat.billboard != 0u) {
        if (dot(shading_n, gl_WorldRayDirectionEXT) > 0.0) {
            shading_n = -shading_n;
        }
    }

    const float SURF_OFFSET = 1e-3;
    vec3 front_pos = pos_world + shading_n * SURF_OFFSET;
    vec3 back_pos  = pos_world - shading_n * SURF_OFFSET;

    uint seed = payload.seed;

    // --------------------------------------------------------
    // Material dispatch
    // --------------------------------------------------------
    switch (mat.type) {

    // ---- Lambertian diffuse ----
    case MAT_LAMBERTIAN: {
        vec3 scatter_dir = rand_cosine_hemisphere(seed, shading_n);
        payload.scatter        = 1u;
        payload.scatter_origin = front_pos;
        payload.scatter_dir    = scatter_dir;
        payload.radiance       = mat.emissive.xyz;
        payload.attenuation    = mat.albedo.xyz;
        break;
    }

    // ---- Specular metal ----
    case MAT_METAL: {
        vec3 incident   = normalize(gl_WorldRayDirectionEXT);
        vec3 reflected  = reflect(incident, shading_n);
        // Fuzzy reflection via roughness (same as CPU)
        reflected = normalize(reflected + rand_unit_sphere(seed) * mat.roughness);

        bool valid = dot(reflected, shading_n) > 0.0;
        payload.scatter        = valid ? 1u : 0u;
        payload.scatter_origin = front_pos;
        payload.scatter_dir    = reflected;
        payload.radiance       = mat.emissive.xyz;
        payload.attenuation    = valid ? mat.albedo.xyz : vec3(0.0);
        break;
    }

    // ---- Dielectric (glass / water / ...) ----
    case MAT_DIELECTRIC: {
        vec3  incident = normalize(gl_WorldRayDirectionEXT);
        float cos_i    = dot(-incident, shading_n);
        bool  backface = cos_i < 0.0;
        vec3  n_corr   = backface ? -shading_n : shading_n;
        cos_i          = abs(cos_i);

        // Simplified single-level IOR tracking (air ↔ glass)
        float eta_i = payload.current_ior;
        float eta_t = backface ? 1.0 : mat.ior;
        float eta   = eta_i / eta_t;

        // Total internal reflection check
        bool tir        = (1.0 - cos_i * cos_i) * eta * eta > 1.0;
        bool do_reflect = tir || (reflectance(cos_i, eta_i, eta_t) > rand_f(seed));

        vec3 next_dir;
        vec3 next_origin;
        float next_ior;

        if (do_reflect) {
            next_dir    = reflect(incident, n_corr);
            next_origin = pos_world + n_corr * SURF_OFFSET;
            next_ior    = eta_i;
        } else {
            next_dir    = refract(incident, n_corr, eta);
            next_origin = pos_world - n_corr * SURF_OFFSET;
            next_ior    = eta_t;
        }

        payload.scatter        = 1u;
        payload.scatter_origin = next_origin;
        payload.scatter_dir    = normalize(next_dir);
        payload.radiance       = mat.emissive.xyz;
        payload.attenuation    = vec3(1.0);  // dielectrics are colorless (no absorption)
        payload.current_ior    = next_ior;
        break;
    }

    // ---- Debug: visualise normals ----
    case MAT_NORMAL: {
        payload.scatter     = 0u;
        payload.radiance    = shading_n * 0.5 + 0.5;
        payload.attenuation = vec3(0.0);
        break;
    }

    // ---- Emissive / area light ----
    case MAT_LIGHT: {
        payload.scatter     = 0u;
        payload.radiance    = mat.emissive.xyz;
        payload.attenuation = vec3(0.0);
        break;
    }

    // ---- Unknown material: magenta error colour ----
    default: {
        payload.scatter     = 0u;
        payload.radiance    = vec3(1.0, 0.0, 1.0);
        payload.attenuation = vec3(0.0);
        break;
    }
    } // switch

    payload.seed = seed;
}
