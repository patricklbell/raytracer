#version 460
#extension GL_EXT_ray_tracing : require

// ============================================================================
// Payload (must match rgen.glsl and rchit.glsl)
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

layout(std140, set = 0, binding = 1) uniform Camera {
    vec4     eye;
    vec4     right;
    vec4     up;
    vec4     forward;
    vec4     viewport;
    uint     samples;
    uint     seed_offset;
    uint     max_bounces;
    uint     ortho;
    float    defocus_x;
    float    defocus_y;
    uint     sky;
    uint     _pad;
} cam;

void main() {
    // No further bouncing from a miss
    payload.scatter    = 0u;
    payload.attenuation = vec3(1.0);

    if (cam.sky == 0u) {
        payload.radiance = vec3(0.0);
        return;
    }

    // Sky gradient matching the CPU rt_cpu_miss implementation:
    //   t = pow(clamp(dir.y, 0, 1), 0.5)
    //   sky = lerp(white, light-blue, t) * (0.7 + 0.25*y)
    vec3  d = normalize(gl_WorldRayDirectionEXT);
    float y = clamp(d.y, 0.0, 1.0);
    float t = pow(y, 0.5);
    vec3  sky = mix(vec3(1.0), vec3(0.5, 0.7, 1.0), t);
    sky *= 0.7 + 0.25 * y;

    payload.radiance = sky;
}
