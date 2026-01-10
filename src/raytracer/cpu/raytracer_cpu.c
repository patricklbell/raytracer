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

rt_hook void rt_tracer_cast(RT_Handle handle, RT_CastSettings settings, vec3_f32* out_radiance, int width, int height) {
    RT_CPU_Tracer* tracer = rt_cpu_handle_to_tracer(handle);
    rt_cpu_raygen(tracer, &settings, out_radiance, width, height);
}

// ============================================================================
// cpu kernels
// ============================================================================
void rt_cpu_raygen(RT_CPU_Tracer* tracer, const RT_CastSettings* s, vec3_f32* out_radiance, int width, int height) {
    vec3_f32 z_extents = (vec3_f32){.xy=s->z_extents, ._z=s->z_near};
    vec3_f32 right = cross_3f32(s->forward, s->up);
    f32 x_norm_sample_size = 1.f/(f32)(width *s->samples);
    f32 y_norm_sample_size = 1.f/(f32)(height*s->samples);
    f32 inv_sample_count = 1.f/((f32)s->samples*s->samples);

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            vec3_f32* c = &out_radiance[y*width + x];
            
            *c = zero_struct;
            for (int y_sample = 0; y_sample < s->samples; y_sample++) {
                for (int x_sample = 0; x_sample < s->samples; x_sample++) {
                    
                    f32 x_norm = ((f32)x/width ) + ((f32)x_sample/s->samples)*x_norm_sample_size;
                    f32 y_norm = ((f32)y/height) + ((f32)y_sample/s->samples)*y_norm_sample_size;
                    // either add blue noise or center the sample
                    if (s->samples > 1) {
                        x_norm += rand_unit_f32()*x_norm_sample_size;
                        y_norm += rand_unit_f32()*y_norm_sample_size;
                    } else {
                        x_norm += 0.5f*x_norm_sample_size;
                        y_norm += 0.5f*y_norm_sample_size;
                    }
                    
                    // @note (0,0) -> TL, (w,h) -> BR
                    vec3_f32 ndc = make_3f32(2.f*x_norm - 1.f, 1.f - 2.f*y_norm, 1.f);
                    vec3_f32 view = elmul_3f32(ndc, z_extents);
                    vec3_f32 near = add_3f32(add_3f32(
                        mul_3f32(right,         view.x),
                        mul_3f32(s->up,         view.y)),
                        mul_3f32(s->forward,    view.z)
                    );
                    
                    RT_CPU_Ray ray;
                    ray.origin = add_3f32(s->eye, near);
                    ray.direction = normalize_3f32(near);
        
                    RT_CPU_HitRecord record;
                    *c = add_3f32(*c, rt_cpu_trace_ray(tracer, &ray, s->max_bounces, rt_cpu_make_pos_interval(), &record));
                }
            }
            *c = mul_3f32(*c, inv_sample_count);
        }
    }
}

vec3_f32 rt_cpu_trace_ray(RT_CPU_Tracer* tracer, const RT_CPU_Ray* in_ray, u8 depth, RT_CPU_Interval interval, RT_CPU_HitRecord* out_record) {
    if (depth == 0) {
        return make_3f32(0.f,0.f,0.f);
    }

    if (rt_cpu_intersect(tracer, in_ray, interval, out_record)) {
        return rt_cpu_closest_hit(tracer, in_ray, depth-1, out_record);
    }
    return rt_cpu_miss(tracer, in_ray, depth-1);
}

vec3_f32 rt_cpu_closest_hit(RT_CPU_Tracer* tracer, const RT_CPU_Ray* in_ray, u8 depth, RT_CPU_HitRecord* in_record) {
    if (rt_is_zero_handle(in_record->material)) {
        return make_3f32(1.f, 0.f, 1.f);
    }

    RT_Material* mat = &((RT_MaterialNode*)in_record->material.v64[0])->v;
    switch (mat->type) {
        case RT_MaterialType_Lambertian:{
            vec3_f32 i = rt_cpu_cosine_sample(in_record->n);

            RT_CPU_Ray i_ray = {
                .origin=add_3f32(in_record->p, mul_3f32(in_record->n, 0.001f)),
                .direction=i,
            };
            RT_CPU_HitRecord i_record;
            
            vec3_f32 i_radiance = rt_cpu_trace_ray(tracer, &i_ray, depth-1, rt_cpu_make_pos_interval(), &i_record);
            return elmul_3f32(i_radiance, mat->albedo);
        }break;
        case RT_MaterialType_Normal:{
            return rt_cpu_normal_to_radiance(in_record->n);
        }break;
    }

    Assert(false);
    return (vec3_f32){};
}

vec3_f32 rt_cpu_miss(RT_CPU_Tracer* tracer, const RT_CPU_Ray* in_ray, u8 depth) {
    f32 y = Clamp(in_ray->direction.y, 0.f, 1.f);
    f32 t = pow_f32(y, 0.5f);

    vec3_f32 sky = lerp_3f32(
        make_3f32(1.f, 1.f, 1.f),   // horizon
        make_3f32(0.5f, 0.7f, 1.f),    // zenith
        t
    );

    // atmospheric darkening near horizon
    sky = mul_3f32(sky, 0.7f + 0.25f * y);
    return sky;
}

// ============================================================================
// helpers
// ============================================================================
bool rt_cpu_intersect(RT_CPU_Tracer* tracer, const RT_CPU_Ray* in_ray, RT_CPU_Interval interval, RT_CPU_HitRecord* out_record) {
    bool hit = false;

    for EachList(en, RT_EntityNode, tracer->world->entities.first) {
        RT_Entity* entity = &en->v;

        // @note out_record is overwritten, assumes hits are always valid
        bool hit_entity = false;
        switch (entity->type) {
            case RT_EntityType_Sphere:{
                hit_entity = rt_cpu_intersect_sphere(tracer, &entity->sphere, in_ray, interval, out_record);
            }break;
        }

        if (hit_entity) {
            hit = true;
            interval.max = out_record->t;
            out_record->material = entity->material;
        }
    }
    
    return hit;
}

bool rt_cpu_intersect_sphere(RT_CPU_Tracer* tracer, const RT_Sphere* in_sphere, const RT_CPU_Ray* in_ray, RT_CPU_Interval interval, RT_CPU_HitRecord* out_record) {
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
    f32 t = (-half_b - half_sqrt_d)/a;
    if (!rt_cpu_in_interval(t, &interval)) {
        t = (-half_b + half_sqrt_d)/a;
        if (!rt_cpu_in_interval(t, &interval))
            return false;
    }

    out_record->p = add_3f32(in_ray->origin, mul_3f32(in_ray->direction, t));
    out_record->t = t;
    out_record->n = mul_3f32(sub_3f32(out_record->p, in_sphere->center), 1.f/in_sphere->radius);

    return true;
}

RT_CPU_Interval rt_cpu_make_pos_interval() {
    return (RT_CPU_Interval){EPSILON_F32, MAX_F32};
}
bool rt_cpu_in_interval(f32 x, const RT_CPU_Interval* in_interval) {
    return x >= in_interval->min && x <= in_interval->max; 
}

vec3_f32 rt_cpu_cosine_sample(vec3_f32 normal) {
    vec3_f32 i = add_3f32(rand_unit_sphere_3f32(), normal);
    bool near_zero = all_3b(leq_3f32_f32(abs_3f32(i), EPSILON_F32));
    return near_zero ? normal : i;
}
vec3_f32 rt_cpu_normal_to_radiance(vec3_f32 normal) {
    return mul_3f32(add_3f32(normal, make_3f32(1.f,1.f,1.f)), 0.5f);
}