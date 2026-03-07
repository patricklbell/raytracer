#version 460
#extension GL_EXT_ray_tracing : require

// ============================================================================
// Payload (must match rmiss.glsl and rchit.glsl)
// ============================================================================
struct RayPayload {
    vec3  radiance;       // emitted/incoming light at this event
    uint  scatter;        // 1 = continue bouncing, 0 = stop
    vec3  attenuation;    // throughput multiplier for the next bounce
    float current_ior;    // IOR of the medium the ray is currently in
    vec3  scatter_origin; // next ray origin
    uint  seed;           // PCG/hash RNG state
    vec3  scatter_dir;    // next ray direction
    float _pad0;
};

layout(location = 0) rayPayloadEXT RayPayload payload;

// ============================================================================
// Bindings
// ============================================================================
layout(set = 0, binding = 0) uniform accelerationStructureEXT tlas;

layout(std140, set = 0, binding = 1) uniform Camera {
    vec4     eye;        // xyz = position
    vec4     right;      // xyz = right direction (unit)
    vec4     up;         // xyz = up direction (unit)
    vec4     forward;    // xyz = forward direction (unit)
    vec4     viewport;   // xyz = (plane_width, plane_height, focus_distance)
    uint     samples;
    uint     seed_offset;
    uint     max_bounces;
    uint     ortho;
    float    defocus_x;
    float    defocus_y;
    uint     sky;
    uint     _pad;
} cam;

layout(set = 0, binding = 2, rgba32f) uniform image2D output_image;

// ============================================================================
// RNG  (finalizing hash, Bob Jenkins style)
// ============================================================================
uint hash_u32(uint x) {
    x += (x << 10u); x ^= (x >>  6u);
    x += (x <<  3u); x ^= (x >> 11u);
    x += (x << 15u);
    return x;
}

uint hash_combine(uint a, uint b) {
    return hash_u32(a ^ (hash_u32(b) + 0x9e3779b9u + (a << 6u) + (a >> 2u)));
}

float rand_f(inout uint s) {
    s = hash_u32(s);
    // map to [0, 1)
    return uintBitsToFloat((s >> 9u) | 0x3f800000u) - 1.0;
}

vec2 rand_unit_disk(inout uint s) {
    // Concentric disk sampling (Shirley)
    float r   = sqrt(rand_f(s));
    float phi = 6.28318530718 * rand_f(s);
    return vec2(cos(phi), sin(phi)) * r;
}

// ============================================================================
// main
// ============================================================================
void main() {
    uvec2 px = gl_LaunchIDEXT.xy;
    uvec2 sz = gl_LaunchSizeEXT.xy;

    uint total_samples = cam.samples * cam.samples;
    vec3 total_radiance = vec3(0.0);

    for (uint s = 0u; s < total_samples; ++s) {
        // Per-sample RNG seed (unique per pixel + sample + frame)
        uint seed = hash_combine(
                        hash_combine(px.x, px.y * 72497u),
                        s + cam.seed_offset * 6791u);

        // Sub-pixel jitter for anti-aliasing
        float jx = rand_f(seed) - 0.5;
        float jy = rand_f(seed) - 0.5;

        // UV in [0,1]^2, Y=0 at top of image
        vec2 uv = (vec2(px) + vec2(0.5) + vec2(jx, jy)) / vec2(sz);

        // NDC in [-1,1]^2, ndc.y = +1 at top (matches project's Y-up convention)
        vec2 ndc = vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);

        // Camera model matches the CPU raygen:
        //   view   = ndc * viewport.xy, focus_dist = viewport.z
        //   sample = eye + right*view.x + up*view.y + forward*view.z
        //   dir    = normalize(sample - origin)
        vec3 ray_origin, ray_dir;

        if (cam.ortho != 0u) {
            // Orthographic: ray origins span the film plane, all rays aim forward
            ray_origin = cam.eye.xyz
                       + ndc.x * cam.viewport.x * cam.right.xyz
                       + ndc.y * cam.viewport.y * cam.up.xyz;
            ray_dir = normalize(cam.forward.xyz);
        } else {
            // Perspective (pinhole)
            vec3 film_pt = cam.eye.xyz
                         + cam.forward.xyz * cam.viewport.z
                         + ndc.x * cam.viewport.x * cam.right.xyz
                         + ndc.y * cam.viewport.y * cam.up.xyz;
            ray_origin = cam.eye.xyz;
            ray_dir    = normalize(film_pt - ray_origin);

            // Depth-of-field (only when defocus radius > 0)
            if (cam.defocus_x > 1e-4) {
                vec2 disk   = rand_unit_disk(seed);
                vec3 offset = cam.right.xyz  * disk.x * cam.defocus_x
                            + cam.up.xyz     * disk.y * cam.defocus_y;
                ray_origin += offset;
                ray_dir     = normalize(film_pt - ray_origin);
            }
        }

        // ----------------------------------------------------------------
        // Iterative path tracing
        // ----------------------------------------------------------------
        payload.scatter_origin = ray_origin;
        payload.scatter_dir    = ray_dir;
        payload.seed           = seed;
        payload.current_ior    = 1.0; // start in air/vacuum
        payload.scatter        = 1u;

        vec3 throughput = vec3(1.0);
        vec3 radiance   = vec3(0.0);

        for (uint b = 0u; b <= cam.max_bounces; ++b) {
            if (payload.scatter == 0u) break;

            // Clear scatter flag; rchit/rmiss will set it if we should continue
            payload.scatter = 0u;

            traceRayEXT(
                tlas,
                gl_RayFlagsOpaqueEXT,
                0xFF,                      // cull mask
                0, 0, 0,                   // sbt offsets / miss index
                payload.scatter_origin,
                1e-3,                      // tMin
                payload.scatter_dir,
                1e7,                       // tMax
                0);                        // payload location

            radiance   += payload.radiance * throughput;
            throughput *= payload.attenuation;

            // Russian roulette (after a few warm-up bounces)
            if (b >= 3u) {
                float q = max(max(throughput.r, throughput.g), throughput.b);
                if (rand_f(payload.seed) > q) break;
                throughput /= max(q, 1e-6);
            }

            if (dot(throughput, throughput) < 1e-8) break;
        }

        total_radiance += radiance;
    }

    total_radiance /= float(total_samples);
    imageStore(output_image, ivec2(px), vec4(total_radiance, 1.0));
}
