RT_CPU_Tracer* rt_cpu_handle_to_tracer(RT_Handle handle) {
    return (RT_CPU_Tracer*)handle.v64[0];
}
RT_Handle rt_cpu_tracer_to_handle(RT_CPU_Tracer* tracer) {
    RT_Handle handle = zero_struct;
    handle.v64[0] = (u64)tracer;
    return handle;
}

rt_hook RT_Handle rt_make_tracer(RT_TracerSettings settings) {
    Arena* arena = arena_alloc();
    RT_CPU_Tracer* tracer = push_array(arena, RT_CPU_Tracer, 1);
    tracer->arena = arena;
    return rt_cpu_tracer_to_handle(tracer);
}
rt_hook void rt_tracer_update_world(RT_Handle handle, RT_World* world) {
    RT_CPU_Tracer* tracer = rt_cpu_handle_to_tracer(handle);
    tracer->world = world;
}
rt_hook void rt_tracer_cleanup(RT_Handle handle) {
    RT_CPU_Tracer* tracer = rt_cpu_handle_to_tracer(handle);
    arena_release(tracer->arena);
}

rt_hook void rt_tracer_cast(RT_Handle handle, RT_CastSettings settings, vec3_f32* out_color, int width, int height) {
    RT_CPU_Tracer* tracer = rt_cpu_handle_to_tracer(handle);
    rt_cpu_raygen(tracer, &settings, out_color, width, height);
}

// ============================================================================
// cpu kernels
// ============================================================================
void rt_cpu_raygen(RT_CPU_Tracer* tracer, const RT_CastSettings* s, vec3_f32* out_color, int width, int height) {
    vec3_f32 z_extents = (vec3_f32){.xy=s->z_extents, ._z=s->z_near};
    vec3_f32 right = cross_3f32(s->forward, s->up);

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            vec3_f32* c = &out_color[y*width + x];

            RT_CPU_Ray ray;
            vec3_f32 ndc = make_3f32(2.f*(f32)x/width - 1.0f, 1.0f - 2.f*(f32)y/height, 1.f);
            vec3_f32 view = elmul_3f32(ndc, z_extents);
            vec3_f32 near = add_3f32(add_3f32(
                mul_3f32(right,         view.x),
                mul_3f32(s->up,         view.y)),
                mul_3f32(s->forward,    view.z)
            );
            ray.origin = add_3f32(s->eye, near);
            ray.direction = normalize_3f32(near);

            RT_CPU_HitRecord record;
            if (rt_cpu_trace_ray(tracer, &ray, (RT_CPU_Interval){0.00001, MAX_F32}, &record)) {
                *c = rt_cpu_closest_hit(tracer, &ray, &record);
            } else {
                *c = rt_cpu_miss(tracer, &ray);
            }
        }
    }
}

bool rt_cpu_trace_ray(RT_CPU_Tracer* tracer, const RT_CPU_Ray* in_ray, RT_CPU_Interval interval, RT_CPU_HitRecord* out_record) {
    RT_Sphere test = {
        .center = make_3f32(0,0,0),
        .radius = 1.0f,
    };
    return rt_cpu_trace_ray_sphere(tracer, &test, in_ray, interval, out_record);
}

vec3_f32 rt_cpu_closest_hit(RT_CPU_Tracer* tracer, const RT_CPU_Ray* ray, RT_CPU_HitRecord* in_record) {
    return make_3f32(1,0,0);
}
vec3_f32 rt_cpu_miss(RT_CPU_Tracer* tracer, const RT_CPU_Ray* ray) {
    return make_3f32(0,0,0);
}

// ============================================================================
// helpers
// ============================================================================
bool rt_cpu_trace_ray_sphere(RT_CPU_Tracer* tracer, const RT_Sphere* in_sphere, const RT_CPU_Ray* in_ray, RT_CPU_Interval interval, RT_CPU_HitRecord* out_record) {
    Assert(in_sphere->radius > 0.f);

    // Sphere: |p - c|^2 = r^2
    // Line  : p = t*d + o
    // => |t*d + o - c|^2 = r^2
    // => (t*d + o - c) . (t*d + o - c) = r^2
    // => t^2*|d|^2 + 2*t*d.(o - c) + |o - c|^2 - r^2 = 0
    // define a, b, c s.t. at^2 + bt + c = 0
    vec3_f32 o_minus_c = sub_3f32(in_ray->origin, in_sphere->center);
    f32 a = length2_3f32(in_ray->direction);
    f32 half_b = dot_3f32(in_ray->direction, o_minus_c);
    f32 c = length2_3f32(o_minus_c) - in_sphere->radius*in_sphere->radius;

    f32 qtr_d = half_b*half_b - a*c;
    if (qtr_d < 0.f)
        return false;
    f32 half_sqrt_d = sqrt_f32(qtr_d);

    // since a is always positive we know - is the closer solution
    f32 t = (-half_b - half_sqrt_d) / a;
    if (!rt_cpu_in_interval(t, &interval)) {
        t = (-half_b + half_sqrt_d) / a;
        if (!rt_cpu_in_interval(t, &interval))
            return false;
    }

    out_record->p = add_3f32(in_ray->origin, mul_3f32(in_ray->direction, t));
    out_record->t = t;
    out_record->n = mul_3f32(sub_3f32(out_record->p, in_sphere->center), 1.f/in_sphere->radius);

    return true;
}

bool rt_cpu_in_interval(f32 x, const RT_CPU_Interval* in_interval) {
    return x >= in_interval->min && x <= in_interval->max; 
}