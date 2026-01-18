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

static rng3_f32 rt_cpu_blas_node_to_aabb(const RT_CPU_BLASNode* node) {
    const RT_Entity* entity = node->entity;

    switch (entity->type) {
        case RT_EntityType_Sphere:{
            vec3_f32 r = make_scale_3f32(entity->sphere.radius);
            return make_rng3_f32(sub_3f32(entity->sphere.center, r), add_3f32(entity->sphere.center, r));
        }break;
        case RT_EntityType_Mesh:{
            if (node->lbvh == NULL) {
                const RT_Mesh* mesh = &entity->mesh;
                
                vec3_f32* p_start = OffsetPtr(mesh->vertices, geo_vertex_offset(mesh->attrs, GEO_VertexAttributes_P), GEO_VertexType_P);
                u64 p_stride = geo_vertex_stride(mesh->attrs, GEO_VertexAttributes_P);
                
                Assert(mesh->vertices_count > 0);
                vec3_f32 min = make_scale_3f32(+MAX_F32), max = make_scale_3f32(-MAX_F32);
                for (u32 idx = 0; idx < mesh->vertices_count; idx++) {
                    vec3_f32 v = *OffsetPtr(p_start, idx*p_stride, GEO_VertexType_P);

                    min = min_3f32(min, v);
                    max = max_3f32(max, v);
                }
                return make_rng3_f32(min, max);
            } else {
                return node->lbvh->root->aabb;
            }
        }break;
    }

    NotImplemented;
    return (rng3_f32){};
}

internal void rt_cpu_build_tlas(RT_CPU_TLAS* out_tlas, Arena* arena, const RT_CPU_BLAS* in_blas) {
    {DeferResource(Temp scratch = scratch_begin(NULL, 0), scratch_end(scratch)) {
        rng3_f32* lbvh_aabbs = push_array_no_zero(scratch.arena, rng3_f32, in_blas->node_count);

        for (u64 idx = 0; idx < in_blas->node_count; idx++) {
            const RT_CPU_BLASNode* node = &in_blas->nodes[idx];
            lbvh_aabbs[idx] = rt_cpu_blas_node_to_aabb(node);
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

                    if (s->samples > 1) {
                        // jitter @todo blue noise
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
                        mul_3f32(s->right,   view.x)),
                        mul_3f32(s->up,      view.y)),
                        mul_3f32(s->forward, view.z)
                    );

                    vec3_f32 origin = (!s->orthographic) ? s->eye : sub_3f32(sample, s->forward);
                    if (s->defocus) {
                        vec2_f32 disk_sample = elmul_2f32(rand_unit_sphere_2f32(), s->defocus_disk);
                        origin = add_3f32(add_3f32(origin,
                            mul_3f32(s->right, disk_sample.x)),
                            mul_3f32(s->up,    disk_sample.y)
                        );
                    }

                    rng3_f32 ray = {
                        .origin = origin,
                        .direction = sub_3f32(sample, origin),
                    };
        
                    RT_CPU_TraceContext ctx = zero_struct;
                    ctx.ior[0] = s->ior;
                    RT_CPU_HitRecord record;
                    *c = add_3f32(*c, rt_cpu_trace_ray(tracer, &ctx, &ray, tracer->max_bounces, geo_make_pos_interval(), &record));
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

    if (mat->billboard && dot_3f32(in_record->n, in_ray->direction) > 0.f) {
        in_record->n = mul_3f32(in_record->n, -1.f);
    }

    switch (mat->type) {
        case RT_MaterialType_Lambertian:{
            rng3_f32 r_ray = {
                .origin = add_3f32(in_record->p, mul_3f32(in_record->n, RT_CPU_SURFACE_OFFSET)),
                .direction = rt_cpu_cosine_sample(in_record->n),
            };
            
            RT_CPU_HitRecord r_record;
            vec3_f32 radiance = rt_cpu_trace_ray(tracer, ctx, &r_ray, depth-1, geo_make_pos_interval(), &r_record);
            return elmul_3f32(radiance, mat->albedo);
        }break;
        case RT_MaterialType_Dieletric:{
            Assert(!mat->billboard);

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
            vec3_f32 radiance = rt_cpu_trace_ray(tracer, ctx, &s_ray, depth-1, geo_make_pos_interval(), &s_record);

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
            
            vec3_f32 radiance = rt_cpu_trace_ray(tracer, ctx, &i_ray, depth-1, geo_make_pos_interval(), &i_record);
            return radiance;
        }break;
        case RT_MaterialType_Normal:{
            return rt_cpu_normal_to_radiance(in_record->n);
        }break;
    }

    NotImplemented;
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
static bool rt_cpu_bvh_hit(u64 id, const rng3_f32* in_ray, rng_f32* inout_t_interval, void* data) {
    RT_CPU_BVHData* rt_cpu_data = (RT_CPU_BVHData*)data;
    RT_CPU_BLAS* blas = &rt_cpu_data->tracer->blas;

    Assert(id > 0 && id <= blas->node_count);
    RT_CPU_BLASNode* blas_node = &blas->nodes[id-1];

    return rt_cpu_intersect_blas_node(blas_node, in_ray, inout_t_interval, &rt_cpu_data->bvh_hit_record);
}

internal bool rt_cpu_intersect(RT_CPU_Tracer* tracer, const rng3_f32* in_ray, rng_f32 interval, RT_CPU_HitRecord* out_record) {
    RT_CPU_BVHData rt_cpu_bvh_data = {
        .bvh_hit_record = {},
        .tracer = tracer,
    };
    bool hit = lbvh_query_ray(&tracer->tlas.lbvh, in_ray, &interval, &rt_cpu_bvh_hit, (void*)&rt_cpu_bvh_data);

    // convert bvh hit record into hit record for shading
    // (avoids costly calculations if multiple intersections occur)
    if (hit) {
        RT_CPU_BVHHitRecord* bvh_hit_record = &rt_cpu_bvh_data.bvh_hit_record;
        const RT_CPU_BLASNode* blas_node = bvh_hit_record->blas_node;
        const RT_Entity* entity = blas_node->entity;

        out_record->t = interval.max;
        out_record->p = add_3f32(in_ray->origin, mul_3f32(in_ray->direction, out_record->t));
        out_record->material = entity->material;
        
        // @todo flag on material showing which attributes are necessary for shading?
        switch (entity->type) {
            case RT_EntityType_Sphere:{
                const RT_Sphere* sphere = &entity->sphere;

                out_record->n = mul_3f32(sub_3f32(out_record->p, sphere->center), 1.f/sphere->radius);
            }break;
            case RT_EntityType_Mesh:{
                const RT_Mesh* mesh = &entity->mesh;

                Assert(mesh->primitive == GEO_Primitive_TRI_LIST); // @todo

                if (!(mesh->attrs & GEO_VertexAttributes_N)) {
                    vec3_f32* p_start = OffsetPtr(mesh->vertices, geo_vertex_offset(mesh->attrs, GEO_VertexAttributes_P), GEO_VertexType_P);
                    u64 p_stride = geo_vertex_stride(mesh->attrs, GEO_VertexAttributes_P);
    
                    vec3_f32 v0,v1,v2;
                    u32 idx = bvh_hit_record->tri_idx;
                    bool auto_index = mesh->indices_count == 0;
                    if (auto_index) {
                        v0 = *OffsetPtr(p_start, (idx+0)*p_stride, GEO_VertexType_P);
                        v1 = *OffsetPtr(p_start, (idx+1)*p_stride, GEO_VertexType_P);
                        v2 = *OffsetPtr(p_start, (idx+2)*p_stride, GEO_VertexType_P);
                    } else {
                        v0 = *OffsetPtr(p_start, (mesh->indices[idx+0])*p_stride, GEO_VertexType_P);
                        v1 = *OffsetPtr(p_start, (mesh->indices[idx+1])*p_stride, GEO_VertexType_P);
                        v2 = *OffsetPtr(p_start, (mesh->indices[idx+2])*p_stride, GEO_VertexType_P);
                    }
    
                    Assert(tracer->winding_order == GEO_WindingOrder_CCW);
                    out_record->n = normalize_3f32(cross_3f32(sub_3f32(v1, v0), sub_3f32(v2, v0)));
                } else {
                    NotImplemented;
                }
            }
        }
    }

    return hit;
}

internal bool rt_cpu_intersect_blas_node(const RT_CPU_BLASNode* blas_node, const rng3_f32* in_ray, rng_f32* inout_t_interval, RT_CPU_BVHHitRecord* out_record) {
    RT_Entity* entity = blas_node->entity;

    bool hit = false;
    switch (entity->type) {
        case RT_EntityType_Sphere:{
            const RT_Sphere* sphere = &entity->sphere;
            hit = geo_intersect_sphere(in_ray, sphere->center, sphere->radius, inout_t_interval);
        }break;
        case RT_EntityType_Mesh:{
            const RT_Mesh* mesh = &entity->mesh;

            vec3_f32* p_start = OffsetPtr(mesh->vertices, geo_vertex_offset(mesh->attrs, GEO_VertexAttributes_P), GEO_VertexType_P);
            u64 p_stride = geo_vertex_stride(mesh->attrs, GEO_VertexAttributes_P);

            Assert(mesh->primitive == GEO_Primitive_TRI_LIST); // @todo
            bool auto_index = mesh->indices_count == 0;

            if (auto_index) {
                for (u32 idx = 0; idx < mesh->vertices_count; idx+=3) {
                    vec3_f32 v0 = *OffsetPtr(p_start, (idx+0)*p_stride, GEO_VertexType_P);
                    vec3_f32 v1 = *OffsetPtr(p_start, (idx+1)*p_stride, GEO_VertexType_P);
                    vec3_f32 v2 = *OffsetPtr(p_start, (idx+2)*p_stride, GEO_VertexType_P);
    
                    if (geo_intersect_tri(in_ray, v0, v1, v2, inout_t_interval)) {
                        hit = true;
                        out_record->tri_idx = idx;
                    }
                }
            } else {
                for (u32 idx = 0; idx < mesh->indices_count; idx+=3) {
                    vec3_f32 v0 = *OffsetPtr(p_start, (mesh->indices[idx+0])*p_stride, GEO_VertexType_P);
                    vec3_f32 v1 = *OffsetPtr(p_start, (mesh->indices[idx+1])*p_stride, GEO_VertexType_P);
                    vec3_f32 v2 = *OffsetPtr(p_start, (mesh->indices[idx+2])*p_stride, GEO_VertexType_P);
    
                    if (geo_intersect_tri(in_ray, v0, v1, v2, inout_t_interval)) {
                        hit = true;
                        out_record->tri_idx = idx;
                    }
                }
            }
        }
    }

    if (hit) {
        out_record->blas_node = blas_node;
    }
    
    return hit;
}

// ============================================================================
// helpers
// ============================================================================
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