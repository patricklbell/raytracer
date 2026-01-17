internal RT_CPU_Tracer* rt_cpu_handle_to_tracer(RT_Handle handle) {
    return (RT_CPU_Tracer*)handle.v64[0];
}
internal RT_Handle rt_cpu_tracer_to_handle(RT_CPU_Tracer* tracer) {
    RT_Handle handle = zero_struct;
    handle.v64[0] = (u64)tracer;
    return handle;
}

rt_hook RT_Handle rt_make_tracer(RT_TracerSettings settings) {
    Assert(settings.max_bounces < RT_MAX_MAX_BOUNCES);

    Arena* arena = arena_alloc();
    RT_CPU_Tracer* tracer = push_array(arena, RT_CPU_Tracer, 1);
    tracer->arena = arena;
    tracer->max_bounces = settings.max_bounces;
    tracer->blas_arena = arena_alloc();
    tracer->tlas_arena = arena_alloc();
    return rt_cpu_tracer_to_handle(tracer);
}
rt_hook void rt_tracer_load_world(RT_Handle handle, RT_World* world) {
    RT_CPU_Tracer* tracer = rt_cpu_handle_to_tracer(handle);
    
    arena_clear(tracer->blas_arena);
    rt_cpu_build_blas(&tracer->blas, tracer->blas_arena, world);
}
rt_hook void rt_tracer_cleanup(RT_Handle handle) {
    RT_CPU_Tracer* tracer = rt_cpu_handle_to_tracer(handle);
    arena_release(tracer->blas_arena);
    arena_release(tracer->tlas_arena);
    arena_release(tracer->arena);
}

rt_hook void rt_tracer_cast(RT_Handle handle, RT_CastSettings settings, vec3_f32* out_radiance, int width, int height) {
    RT_CPU_Tracer* tracer = rt_cpu_handle_to_tracer(handle);

    arena_clear(tracer->tlas_arena);
    rt_cpu_build_tlas(&tracer->tlas, tracer->tlas_arena, &tracer->blas);

    rt_cpu_raygen(tracer, &settings, out_radiance, width, height);
}

// ============================================================================
// acceleration structures
// ============================================================================
static LBVH_Tree* rt_cpu_entity_to_lbvh_or_null(RT_Entity* entity) {
    return NULL; // @todo
}

internal void rt_cpu_build_blas(RT_CPU_BLAS* out_blas, Arena* arena, RT_World* world) {
    RT_EntityList* list = &world->entities;

    out_blas->node_count = list->length;
    out_blas->nodes = push_array_no_zero(arena, RT_CPU_BLASNode, out_blas->node_count);

    u64 idx = 0;
    for EachList(node, RT_EntityNode, list->first) {
        RT_Entity* entity = &node->v;

        out_blas->nodes[idx].entity = entity;
        out_blas->nodes[idx].lbvh = rt_cpu_entity_to_lbvh_or_null(entity);
        idx++;
    }
}

static rng3_f32 rt_cpu_entity_to_aabb(RT_Entity* entity) {
    switch (entity->type) {
        case RT_EntityType_Sphere:{
            vec3_f32 r = make_scale_3f32(entity->sphere.radius);
            return make_rng3_f32(sub_3f32(entity->sphere.center, r), add_3f32(entity->sphere.center, r));
        }break;
    }

    AssertAlways(false);
    return (rng3_f32){};
}

internal void rt_cpu_build_tlas(RT_CPU_TLAS* out_tlas, Arena* arena, const RT_CPU_BLAS* in_blas) {
    {DeferResource(Temp scratch = scratch_begin(NULL, 0), scratch_end(scratch)) {
        rng3_f32* lbvh_aabbs = push_array_no_zero(scratch.arena, rng3_f32, in_blas->node_count);

        for (u64 idx = 0; idx < in_blas->node_count; idx++) {
            const RT_CPU_BLASNode* node = &in_blas->nodes[idx];
            lbvh_aabbs[idx] = rt_cpu_entity_to_aabb(node->entity);
        }

        out_tlas->lbvh = lbvh_make(arena, lbvh_aabbs, in_blas->node_count);
    }}

    #ifdef BUILD_DEBUG
        lbvh_dump_to_file(&out_tlas->lbvh, "out.bvh");
    #endif
}

// ============================================================================
// cpu kernels
// ============================================================================
internal void rt_cpu_raygen(RT_CPU_Tracer* tracer, const RT_CastSettings* s, vec3_f32* out_radiance, int width, int height) {
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
                    vec3_f32 view = elmul_3f32(ndc, s->viewport);
                    
                    // map to world space and defocus
                    vec3_f32 sample = add_3f32(add_3f32(add_3f32(
                        s->eye,
                        mul_3f32(right,      view.x)),
                        mul_3f32(s->up,      view.y)),
                        mul_3f32(s->forward, view.z)
                    );
                    vec3_f32 origin;
                    if (s->defocus) {
                        vec2_f32 disk_sample = elmul_2f32(rand_unit_sphere_2f32(), s->defocus_disk);
                        origin = add_3f32(add_3f32(s->eye,
                            mul_3f32(right, disk_sample.x)),
                            mul_3f32(s->up, disk_sample.y)
                        );
                    } else {
                        origin = s->eye;
                    }

                    rng3_f32 ray = {
                        .origin = origin,
                        .direction = sub_3f32(sample, origin),
                    };
        
                    RT_CPU_TraceContext ctx = zero_struct;
                    ctx.ior[0] = s->ior;
                    RT_CPU_HitRecord record;
                    *c = add_3f32(*c, rt_cpu_trace_ray(tracer, &ctx, &ray, tracer->max_bounces, rt_cpu_make_pos_interval(), &record));
                }
            }
            *c = mul_3f32(*c, inv_sample_count);
        }
    }
}

internal vec3_f32 rt_cpu_trace_ray(RT_CPU_Tracer* tracer, RT_CPU_TraceContext* ctx, const rng3_f32* in_ray, u8 depth, rng_f32 interval, RT_CPU_HitRecord* out_record) {
    if (depth == 0) {
        return make_scale_3f32(0.f);
    }

    if (rt_cpu_intersect(tracer, in_ray, interval, out_record)) {
        return rt_cpu_closest_hit(tracer, ctx, in_ray, depth, out_record);
    }
    return rt_cpu_miss(tracer, ctx, in_ray, depth);
}

#define RT_CPU_SURFACE_OFFSET 0.001f

internal vec3_f32 rt_cpu_closest_hit(RT_CPU_Tracer* tracer, RT_CPU_TraceContext* ctx, const rng3_f32* in_ray, u8 depth, RT_CPU_HitRecord* in_record) {
    if (rt_is_zero_handle(in_record->material)) {
        return make_scale_3f32(1.f);
    }

    RT_Material* mat = &((RT_MaterialNode*)in_record->material.v64[0])->v;
    switch (mat->type) {
        case RT_MaterialType_Lambertian:{
            rng3_f32 r_ray = {
                .origin = add_3f32(in_record->p, mul_3f32(in_record->n, RT_CPU_SURFACE_OFFSET)),
                .direction = rt_cpu_cosine_sample(in_record->n),
            };
            
            RT_CPU_HitRecord r_record;
            vec3_f32 radiance = rt_cpu_trace_ray(tracer, ctx, &r_ray, depth-1, rt_cpu_make_pos_interval(), &r_record);
            return elmul_3f32(radiance, mat->albedo);
        }break;
        case RT_MaterialType_Dieletric:{
            vec3_f32 d_norm = normalize_3f32(in_ray->direction);
            f32 idotn = -dot_3f32(in_record->n, d_norm);
            bool backface = idotn < 0.f;
            vec3_f32 n_corr = mul_3f32(in_record->n, backface ? -1.f : 1.f);

            f32 eta_i = ctx->ior[ctx->ior_count];
            f32 eta_t = backface ? mat->ior : ctx->ior[Max(ctx->ior_count-1, 0)];
            f32 eta = eta_i / eta_t;
            bool tir = sqrt_f32(1-idotn*idotn)*eta > 1.f;
            bool reflect = tir || rt_cpu_fresnel_schlick(eta_i, eta_t, abs_f32(idotn)) > rand_unit_f32();

            rng3_f32 s_ray = *in_ray;
            if (reflect) {
                s_ray = {
                    .origin=add_3f32(in_record->p, mul_3f32(n_corr, RT_CPU_SURFACE_OFFSET)),
                    .direction = reflect_3f32(d_norm, n_corr),
                };
            } else {
                s_ray = {
                    .origin=add_3f32(in_record->p, mul_3f32(n_corr, -RT_CPU_SURFACE_OFFSET)),
                    .direction = refract_3f32(d_norm, n_corr, eta),
                };
                ctx->ior_count++;
                ctx->ior[ctx->ior_count] = eta_t;
            }

            RT_CPU_HitRecord s_record;
            vec3_f32 radiance = rt_cpu_trace_ray(tracer, ctx, &s_ray, depth-1, rt_cpu_make_pos_interval(), &s_record);

            if (!reflect) {
                ctx->ior_count--;
            }
            return radiance;
        }break;
        case RT_MaterialType_Metal:{
            vec3_f32 i = normalize_3f32(reflect_3f32(in_ray->direction, in_record->n));
            // approximation of specular lobe
            i = add_3f32(i, mul_3f32(rand_unit_sphere_3f32(), mat->roughness));

            rng3_f32 i_ray = {
                .origin=add_3f32(in_record->p, mul_3f32(in_record->n, RT_CPU_SURFACE_OFFSET)),
                .direction=i,
            };
            RT_CPU_HitRecord i_record;
            
            vec3_f32 radiance = rt_cpu_trace_ray(tracer, ctx, &i_ray, depth-1, rt_cpu_make_pos_interval(), &i_record);
            return radiance;
        }break;
        case RT_MaterialType_Normal:{
            return rt_cpu_normal_to_radiance(in_record->n);
        }break;
    }
    return make_3f32(0,0,0);
}

internal vec3_f32 rt_cpu_miss(RT_CPU_Tracer* tracer, RT_CPU_TraceContext* ctx, const rng3_f32* in_ray, u8 depth) {
    f32 y = Clamp(in_ray->direction.y, 0.f, 1.f);
    f32 t = pow_f32(y, 0.5f);

    vec3_f32 sky = lerp_3f32(make_3f32(1.f, 1.f, 1.f), make_3f32(0.5f, 0.7f, 1.f), t);

    // atmospheric darkening near horizon
    sky = mul_3f32(sky, 0.7f + 0.25f * y);
    return sky;
}

// ============================================================================
// intersection
// ============================================================================
struct RT_CPU_BVHData {
    RT_CPU_HitRecord* hit_record;
    RT_CPU_Tracer* tracer;
};
static bool rt_cpu_bvh_hit(u64 id, const rng3_f32* in_ray, rng_f32* inout_t_interval, void* data) {
    RT_CPU_BVHData* rt_cpu_data = (RT_CPU_BVHData*)data;
    RT_CPU_BLAS* blas = &rt_cpu_data->tracer->blas;

    Assert(id != 0 && id <= blas->node_count);
    RT_CPU_BLASNode* blas_node = &blas->nodes[id-1];
    RT_Entity* entity = blas_node->entity;

    bool hit = false;
    switch (entity->type) {
        case RT_EntityType_Sphere:{
            hit = rt_cpu_intersect_sphere(rt_cpu_data->tracer, &entity->sphere, in_ray, *inout_t_interval, rt_cpu_data->hit_record);
        }break;
    }

    if (hit) {
        inout_t_interval->max = rt_cpu_data->hit_record->t;
        rt_cpu_data->hit_record->material = entity->material;
    }
    return hit;
}

internal bool rt_cpu_intersect(RT_CPU_Tracer* tracer, const rng3_f32* in_ray, rng_f32 interval, RT_CPU_HitRecord* out_record) {
    RT_CPU_BVHData rt_cpu_bvh_data = {
        .hit_record = out_record,
        .tracer = tracer,
    };
    return lbvh_query_ray(&tracer->tlas.lbvh, in_ray, &interval, &rt_cpu_bvh_hit, (void*)&rt_cpu_bvh_data);
}

internal bool rt_cpu_intersect_sphere(RT_CPU_Tracer* tracer, const RT_Sphere* in_sphere, const rng3_f32* in_ray, rng_f32 interval, RT_CPU_HitRecord* out_record) {
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

// ============================================================================
// helpers
// ============================================================================
internal rng_f32 rt_cpu_make_pos_interval() {
    return (rng_f32){EPSILON_F32, MAX_F32};
}
internal bool rt_cpu_in_interval(f32 x, const rng_f32* in_interval) {
    return x >= in_interval->min && x <= in_interval->max; 
}

internal vec3_f32 rt_cpu_cosine_sample(vec3_f32 normal) {
    vec3_f32 i = add_3f32(rand_unit_sphere_3f32(), normal);
    bool near_zero = all_3b(leq_3f32_f32(abs_3f32(i), EPSILON_F32));
    return near_zero ? normal : i;
}
internal f32 rt_cpu_fresnel_schlick(f32 eta_i, f32 eta_t, f32 cos_theta) {
    f32 sqrt_R0 = (eta_i - eta_t) / (eta_i + eta_t);
    f32 R0 = sqrt_R0*sqrt_R0;
    return R0 + (1.f- R0)*pow_f32(1.f - cos_theta, 5.f);
}
internal vec3_f32 rt_cpu_normal_to_radiance(vec3_f32 normal) {
    return mul_3f32(add_3f32(normal, make_3f32(1.f,1.f,1.f)), 0.5f);
}