#ifdef BUILD_DEBUG
    #include "extra/dump.c"
#endif

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
    tracer->sky = settings.sky;
    tracer->blas_arena = arena_alloc();
    tracer->tlas_arena = arena_alloc();
    return rt_cpu_tracer_to_handle(tracer);
}
rt_hook void rt_tracer_build_blas(RT_Handle handle, RT_World* world) {
    RT_CPU_Tracer* tracer = rt_cpu_handle_to_tracer(handle);
    
    arena_clear(tracer->blas_arena);
    rt_cpu_build_blas(&tracer->blas, tracer->blas_arena, world);
}
rt_hook void rt_tracer_build_tlas(RT_Handle handle, RT_World* world) {
    RT_CPU_Tracer* tracer = rt_cpu_handle_to_tracer(handle);

    arena_clear(tracer->tlas_arena);
    rt_cpu_build_tlas(&tracer->tlas, tracer->tlas_arena, &tracer->blas, world);
}
rt_hook void rt_tracer_cleanup(RT_Handle handle) {
    RT_CPU_Tracer* tracer = rt_cpu_handle_to_tracer(handle);
    arena_release(tracer->blas_arena);
    arena_release(tracer->tlas_arena);
    arena_release(tracer->arena);
}

rt_hook void rt_tracer_cast(RT_Handle handle, RT_CastSettings settings, vec3_f32* out_radiance, int width, int height) {
    RT_CPU_Tracer* tracer = rt_cpu_handle_to_tracer(handle);

    rt_cpu_raygen(tracer, &settings, out_radiance, width, height);
}

// ============================================================================
// acceleration structures
// ============================================================================
static rng3_f32 rt_cpu_aabb_from_tri(vec3_f32 v0, vec3_f32 v1, vec3_f32 v2) {
    return make_rng3_f32(min_3f32(min_3f32(v0, v1), v2), max_3f32(max_3f32(v0, v1), v2));
}
static void rt_cpu_blas_node_from_mesh(RT_CPU_BLASNode* out_node, Arena* arena, const RT_Mesh* mesh) {
    out_node->mesh = mesh;
    out_node->auto_index = mesh->indices_count == 0;
    
    {DeferResource(Temp scratch = scratch_begin(NULL, 0), scratch_end(scratch)) {
        Assert(mesh->primitive == GEO_Primitive_TRI_LIST); // @todo
        u64 tris_count = out_node->auto_index ? mesh->vertices_count/3 : mesh->indices_count/3;
        rng3_f32* tri_aabbs = push_array_no_zero(scratch.arena, rng3_f32, tris_count);

        {
            vec3_f32* p_start = OffsetPtr(mesh->vertices, geo_vertex_offset(mesh->attrs, GEO_VertexAttributes_P), GEO_VertexType_P);
            u64 p_stride = geo_vertex_stride(mesh->attrs, GEO_VertexAttributes_P);
    
            Assert(mesh->primitive == GEO_Primitive_TRI_LIST); // @todo
            if (out_node->auto_index) {
                for (u32 idx = 0; idx < mesh->vertices_count; idx+=3) {
                    vec3_f32 v0 = *OffsetPtr(p_start, (idx+0)*p_stride, GEO_VertexType_P);
                    vec3_f32 v1 = *OffsetPtr(p_start, (idx+1)*p_stride, GEO_VertexType_P);
                    vec3_f32 v2 = *OffsetPtr(p_start, (idx+2)*p_stride, GEO_VertexType_P);
    
                    tri_aabbs[idx/3] = rt_cpu_aabb_from_tri(v0, v1, v2);
                }
            } else {
                for (u32 idx = 0; idx < mesh->indices_count; idx+=3) {
                    vec3_f32 v0 = *OffsetPtr(p_start, (mesh->indices[idx+0])*p_stride, GEO_VertexType_P);
                    vec3_f32 v1 = *OffsetPtr(p_start, (mesh->indices[idx+1])*p_stride, GEO_VertexType_P);
                    vec3_f32 v2 = *OffsetPtr(p_start, (mesh->indices[idx+2])*p_stride, GEO_VertexType_P);
    
                    tri_aabbs[idx/3] = rt_cpu_aabb_from_tri(v0, v1, v2);
                }
            }
        }

        out_node->lbvh = lbvh_make(arena, tri_aabbs, tris_count);
    }}
}

internal void rt_cpu_build_blas(RT_CPU_BLAS* out_blas, Arena* arena, RT_World* world) {
    RT_MeshList* meshes = &world->meshes;

    out_blas->node_count = meshes->length;
    out_blas->nodes = push_array_no_zero(arena, RT_CPU_BLASNode, out_blas->node_count);

    u64 idx = 0;
    for EachList(node, RT_MeshNode, meshes->first) {
        RT_Mesh* mesh = &node->v;

        rt_cpu_blas_node_from_mesh(&out_blas->nodes[idx], arena, mesh);
        mesh->blas_id = idx;

        idx++;
    }
}

static void rt_cpu_tlas_node_from_instance(RT_CPU_TLASNode* out_node, const RT_Instance* instance, const RT_CPU_BLAS* in_blas, RT_World* world) {
    out_node->instance = instance;

    switch (instance->type) {
        case RT_InstanceType_Mesh:{
            RT_Mesh* mesh = rt_world_resolve_mesh(world, instance->mesh.handle);
            out_node->blas_node = &in_blas->nodes[mesh->blas_id];
        }break;
        case RT_InstanceType_Sphere:{
        }break;
    }
}

static rng3_f32 rt_cpu_tlas_node_to_aabb(const RT_CPU_TLASNode* in_tlas_node) {
    const RT_Instance* instance = in_tlas_node->instance;

    switch (instance->type) {
        case RT_InstanceType_Sphere:{
            vec3_f32 r = make_scale_3f32(instance->sphere.radius);
            return make_rng3_f32(sub_3f32(instance->sphere.center, r), add_3f32(instance->sphere.center, r));
        }break;
        case RT_InstanceType_Mesh:{
            rng3_f32 model_aabb = in_tlas_node->blas_node->lbvh.root->aabb;
            return rt_cpu_transform_aabb(model_aabb, instance->mesh.translation, instance->mesh.rotation, instance->mesh.scale);
        }break;
    }

    NotImplemented;
    return (rng3_f32){};
}

internal void rt_cpu_build_tlas(RT_CPU_TLAS* out_tlas, Arena* arena, const RT_CPU_BLAS* in_blas, RT_World* world) {
    RT_InstanceList* instances = &world->instances;

    out_tlas->node_count = instances->length;
    out_tlas->nodes = push_array_no_zero(arena, RT_CPU_TLASNode, out_tlas->node_count);

    {DeferResource(Temp scratch = scratch_begin(NULL, 0), scratch_end(scratch)) {
        rng3_f32* world_aabbs = push_array_no_zero(scratch.arena, rng3_f32, instances->length);

        u64 idx = 0;
        for EachList(node, RT_InstanceNode, instances->first) {
            const RT_Instance* instance = &node->v;

            rt_cpu_tlas_node_from_instance(&out_tlas->nodes[idx], instance, in_blas, world);
            world_aabbs[idx] = rt_cpu_tlas_node_to_aabb(&out_tlas->nodes[idx]);

            idx++;
        }
        
        out_tlas->lbvh = lbvh_make(arena, world_aabbs, instances->length);
    }}

    #ifdef BUILD_DEBUG
        lbvh_dump_tree(&out_tlas->lbvh, "out.bvh");
    #endif
}

// ============================================================================
// cpu kernels
// ============================================================================
internal void rt_cpu_raygen(RT_CPU_Tracer* tracer, const RT_CastSettings* s, vec3_f32* out_radiance, int width, int height) {
#if BUILD_DEBUG
    rt_cpu_dump_begin_ray_hit_record("out.rays");
#endif

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
                        .direction = normalize_3f32(sub_3f32(sample, origin)),
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
    Assert(abs_f32(length2_3f32(in_ray->direction) - 1) < 0.001f);

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
#if BUILD_DEBUG
    rt_cpu_dump_add_ray_hit_record(in_ray, in_record, "out.rays");
#endif

    if (rt_is_zero_handle(in_record->material)) {
        return make_scale_3f32(1.f);
    }

    RT_Material* mat = &((RT_MaterialNode*)in_record->material.v64[0])->v;

    if (mat->billboard && dot_3f32(in_record->n, in_ray->direction) > 0.f) {
        in_record->n = mul_3f32(in_record->n, -1.f);
    }

    switch (mat->type) {
        case RT_MaterialType_Lambertian:{
            rng3_f32 s_ray = {
                .origin = add_3f32(in_record->p, mul_3f32(in_record->n, RT_CPU_SURFACE_OFFSET)),
                .direction = rt_cpu_cosine_sample_hemisphere(in_record->n),
            };
            
            RT_CPU_HitRecord r_record;
            vec3_f32 i_radiance = rt_cpu_trace_ray(tracer, ctx, &s_ray, depth-1, geo_make_pos_interval(), &r_record);
            vec3_f32 brdf_times_pi = mat->albedo;

            // drop extra terms since pdf = cos_theta / PI
            vec3_f32 s_radiance = elmul_3f32(brdf_times_pi, i_radiance); // * (cos_theta / PI) / pdf
            vec3_f32 e_radiance = mat->emissive;

            return add_3f32(s_radiance, e_radiance);
        }break;
        case RT_MaterialType_Dieletric:{
            Assert(!mat->billboard);

            f32 idotn = -dot_3f32(in_record->n, in_ray->direction);
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
                    .direction = reflect_3f32(in_ray->direction, n_corr),
                };
            } else {
                s_ray = {
                    .origin=add_3f32(in_record->p, mul_3f32(n_corr, -RT_CPU_SURFACE_OFFSET)),
                    .direction = refract_3f32(in_ray->direction, n_corr, eta),
                };
                ctx->ior_count++;
                ctx->ior[ctx->ior_count] = eta_t;
            }

            RT_CPU_HitRecord s_record;
            vec3_f32 s_radiance = rt_cpu_trace_ray(tracer, ctx, &s_ray, depth-1, geo_make_pos_interval(), &s_record);
            vec3_f32 e_radiance = mat->emissive;

            if (!reflect) {
                ctx->ior_count--;
            }

                        return add_3f32(s_radiance, e_radiance);
        }break;
        case RT_MaterialType_Metal:{
            vec3_f32 i = reflect_3f32(in_ray->direction, in_record->n);
            // approximation of specular lobe
            i = add_3f32(i, mul_3f32(rand_unit_sphere_3f32(), mat->roughness));

            rng3_f32 s_ray = {
                .origin=add_3f32(in_record->p, mul_3f32(in_record->n, RT_CPU_SURFACE_OFFSET)),
                .direction=normalize_3f32(i),
            };
            RT_CPU_HitRecord i_record;
            
            return rt_cpu_trace_ray(tracer, ctx, &s_ray, depth-1, geo_make_pos_interval(), &i_record);
        }break;
        case RT_MaterialType_Normal:{
            return rt_cpu_normal_to_radiance(in_record->n);
        }break;
        case RT_MaterialType_Light:{
            return mat->emissive;
        }break;
    }

    NotImplemented;
    return make_3f32(0,0,0);
}

internal vec3_f32 rt_cpu_miss(RT_CPU_Tracer* tracer, RT_CPU_TraceContext* ctx, const rng3_f32* in_ray, u8 depth) {
#if BUILD_DEBUG
    rt_cpu_dump_add_ray_miss_record(in_ray, "out.rays");
#endif

    if (!tracer->sky) {
        return make_scale_3f32(0.f);
    }

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
static bool rt_cpu_tlas_hit(u64 id, const rng3_f32* in_ray, rng_f32* inout_t_interval, void* _data) {
    RT_CPU_TLASData* data = (RT_CPU_TLASData*)_data;

    Assert(id > 0 && id <= data->tlas->node_count);
    RT_CPU_TLASNode* node = &data->tlas->nodes[id-1];

    return rt_cpu_intersect_tlas_node(node, in_ray, inout_t_interval, &data->hit_record);
}

internal bool rt_cpu_intersect(RT_CPU_Tracer* tracer, const rng3_f32* in_ray, rng_f32 interval, RT_CPU_HitRecord* out_record) {
    RT_CPU_TLASData tlas_data = {
        .hit_record = {},
        .tlas = &tracer->tlas,
    };
    bool hit = lbvh_query_ray(&tracer->tlas.lbvh, in_ray, &interval, &rt_cpu_tlas_hit, (void*)&tlas_data);

    // convert tlas hit record into hit record
    // (avoids costly calculations if multiple intersections occur)
    if (hit) {
        RT_CPU_TLASHitRecord* hit_record = &tlas_data.hit_record;
        const RT_CPU_TLASNode* tlas_node = hit_record->tlas_node;
        const RT_Instance* instance = tlas_node->instance;

        out_record->t = interval.max;
        out_record->p = add_3f32(in_ray->origin, mul_3f32(in_ray->direction, out_record->t));
        out_record->material = instance->material;
        
        // @todo flag on material showing which attributes are necessary for shading?
        switch (instance->type) {
            case RT_InstanceType_Sphere:{
                const RT_SphereInstance* sphere_inst = &instance->sphere;

                out_record->n = mul_3f32(sub_3f32(out_record->p, sphere_inst->center), 1.f/sphere_inst->radius);
            }break;
            case RT_InstanceType_Mesh:{
                const RT_MeshInstance* mesh_inst = &instance->mesh;
                const RT_CPU_BLASNode* blas_node = tlas_node->blas_node;
                const RT_Mesh* mesh = blas_node->mesh;

                Assert(mesh->primitive == GEO_Primitive_TRI_LIST); // @todo

                if (!(mesh->attrs & GEO_VertexAttributes_N)) {
                    vec3_f32* p_start = OffsetPtr(mesh->vertices, geo_vertex_offset(mesh->attrs, GEO_VertexAttributes_P), GEO_VertexType_P);
                    u64 p_stride = geo_vertex_stride(mesh->attrs, GEO_VertexAttributes_P);
    
                    vec3_f32 v0,v1,v2;
                    u32 idx = hit_record->tri_idx;
                    if (blas_node->auto_index) {
                        v0 = *OffsetPtr(p_start, (idx+0)*p_stride, GEO_VertexType_P);
                        v1 = *OffsetPtr(p_start, (idx+1)*p_stride, GEO_VertexType_P);
                        v2 = *OffsetPtr(p_start, (idx+2)*p_stride, GEO_VertexType_P);
                    } else {
                        v0 = *OffsetPtr(p_start, (mesh->indices[idx+0])*p_stride, GEO_VertexType_P);
                        v1 = *OffsetPtr(p_start, (mesh->indices[idx+1])*p_stride, GEO_VertexType_P);
                        v2 = *OffsetPtr(p_start, (mesh->indices[idx+2])*p_stride, GEO_VertexType_P);
                    }
    
                    Assert(tracer->winding_order == GEO_WindingOrder_CCW);
                    vec3_f32 tri_n = cross_3f32(sub_3f32(v1, v0), sub_3f32(v2, v0));
                    out_record->n = normalize_3f32(rt_cpu_transform_dir(tri_n, mesh_inst->rotation, mesh_inst->scale));
                } else {
                    NotImplemented;
                }
            }
        }
    }

    return hit;
}

static bool rt_cpu_blas_node_hit(u64 id, const rng3_f32* in_ray, rng_f32* inout_t_interval, void* _data) {
    RT_CPU_BLASNodeData* data = (RT_CPU_BLASNodeData*)_data;

    Assert(data->mesh->primitive == GEO_Primitive_TRI_LIST); // @todo

    bool hit = false;
    if (data->auto_index) {
        Assert(id > 0 && id <= data->mesh->vertices_count/3);
        u64 idx = (id - 1)*3;

        vec3_f32 v0 = *OffsetPtr(data->p_start, (idx+0)*data->p_stride, GEO_VertexType_P);
        vec3_f32 v1 = *OffsetPtr(data->p_start, (idx+1)*data->p_stride, GEO_VertexType_P);
        vec3_f32 v2 = *OffsetPtr(data->p_start, (idx+2)*data->p_stride, GEO_VertexType_P);

        if (geo_intersect_tri(in_ray, v0, v1, v2, inout_t_interval)) {
            hit = true;
            data->hit_record.tri_idx = idx;
        }
    } else {
        Assert(id > 0 && id <= data->mesh->indices_count/3);
        u64 idx = (id - 1)*3;

        vec3_f32 v0 = *OffsetPtr(data->p_start, (data->mesh->indices[idx+0])*data->p_stride, GEO_VertexType_P);
        vec3_f32 v1 = *OffsetPtr(data->p_start, (data->mesh->indices[idx+1])*data->p_stride, GEO_VertexType_P);
        vec3_f32 v2 = *OffsetPtr(data->p_start, (data->mesh->indices[idx+2])*data->p_stride, GEO_VertexType_P);

        if (geo_intersect_tri(in_ray, v0, v1, v2, inout_t_interval)) {
            hit = true;
            data->hit_record.tri_idx = idx;
        }
    }

    return hit;
}

internal bool rt_cpu_intersect_tlas_node(const RT_CPU_TLASNode* tlas_node, const rng3_f32* in_ray, rng_f32* inout_t_interval, RT_CPU_TLASHitRecord* out_record) {
    const RT_Instance* instance = tlas_node->instance;

    bool hit = false;
    switch (instance->type) {
        case RT_InstanceType_Sphere:{
            const RT_SphereInstance* sphere_inst = &instance->sphere;
            hit = geo_intersect_sphere(in_ray, sphere_inst->center, sphere_inst->radius, inout_t_interval);
        }break;
        case RT_InstanceType_Mesh:{
            const RT_MeshInstance* mesh_inst = &instance->mesh;
            const RT_CPU_BLASNode* blas_node = tlas_node->blas_node;
            const RT_Mesh* mesh = blas_node->mesh;

            RT_CPU_BLASNodeData blas_node_data = {
                .hit_record = {},
                .p_start = OffsetPtr(mesh->vertices, geo_vertex_offset(mesh->attrs, GEO_VertexAttributes_P), GEO_VertexType_P),
                .p_stride = geo_vertex_stride(mesh->attrs, GEO_VertexAttributes_P),
                .auto_index = blas_node->auto_index,
                .mesh = mesh,
            };

            // transform to local (model) space
            rng3_f32 local_ray = rt_cpu_inv_transform_ray(*in_ray, mesh_inst->translation, mesh_inst->rotation, mesh_inst->scale);
            f32 t_local_to_world = length_3f32(elmul_3f32(local_ray.direction, mesh_inst->scale));
            Assert(abs_f32(t_local_to_world) > EPSILON_F32);
            f32 t_world_to_local = 1.f/t_local_to_world;
            rng_f32 local_interval = {inout_t_interval->min*t_world_to_local, inout_t_interval->max*t_world_to_local};

            hit = lbvh_query_ray(&blas_node->lbvh, &local_ray, &local_interval, &rt_cpu_blas_node_hit, (void*)&blas_node_data);
            if (hit) {
                out_record->tri_idx = blas_node_data.hit_record.tri_idx;
                inout_t_interval->max = local_interval.max*t_local_to_world;
            }
        }
    }
    if (hit) {
        out_record->tlas_node = tlas_node;
    }
    
    return hit;
}

// ============================================================================
// helpers
// ============================================================================
static vec3_f32 rt_cpu_transform_uvw_to_hemisphere(vec3_f32 s, vec3_f32 n) {
    // @perf
    vec3_f32 u_basis = orthogonal_3f32(n);
    vec3_f32 v_basis = cross_3f32(u_basis, n);
    vec3_f32 w_basis = n;

    return add_3f32(add_3f32(
        mul_3f32(u_basis, s.U),
        mul_3f32(v_basis, s.V)),
        mul_3f32(w_basis, s.W)
    );
}

internal vec3_f32 rt_cpu_cosine_sample_hemisphere(vec3_f32 n) {
    f32 r1 = rand_unit_f32();
    f32 r2 = rand_unit_f32();

    f32 phi = 2*PI_F32*r1;
    f32 u = cos_f32(phi)*sqrt_f32(r2);
    f32 v = sin_f32(phi)*sqrt_f32(r2);
    f32 w = sqrt_f32(1 - r2);

    return rt_cpu_transform_uvw_to_hemisphere((vec3_f32){.U=u,.V=v,.W=w}, n);
}
internal f32 rt_cpu_fresnel_schlick(f32 eta_i, f32 eta_t, f32 cos_theta) {
    f32 sqrt_R0 = (eta_i - eta_t) / (eta_i + eta_t);
    f32 R0 = sqrt_R0*sqrt_R0;
    return R0 + (1.f- R0)*pow_f32(1.f - cos_theta, 5.f);
}
internal vec3_f32 rt_cpu_normal_to_radiance(vec3_f32 normal) {
    return mul_3f32(add_3f32(normal, make_3f32(1.f,1.f,1.f)), 0.5f);
}

internal vec3_f32 rt_cpu_transform_point(vec3_f32 p, vec3_f32 translation, vec4_f32 rotation, vec3_f32 scale) {
    return add_3f32(rot_quat(elmul_3f32(p, scale), rotation), translation);
}
internal vec3_f32 rt_cpu_inv_transform_point(vec3_f32 p, vec3_f32 translation, vec4_f32 rotation, vec3_f32 scale) {
    return eldiv_3f32(rot_quat(sub_3f32(p, translation), inv_quat(rotation)), scale);
}
internal vec3_f32 rt_cpu_transform_dir(vec3_f32 p, vec4_f32 rotation, vec3_f32 scale) {
    return rot_quat(elmul_3f32(p, scale), rotation);
}
internal vec3_f32 rt_cpu_inv_transform_dir(vec3_f32 p, vec4_f32 rotation, vec3_f32 scale) {
    return eldiv_3f32(rot_quat(p, inv_quat(rotation)), scale);
}

// @todo store aabb as center + half-extents
internal rng3_f32 rt_cpu_transform_aabb(rng3_f32 aabb, vec3_f32 translation, vec4_f32 rotation, vec3_f32 scale) {
    vec3_f32 corners[8] = {
        {aabb.min.x, aabb.min.y, aabb.min.z},
        {aabb.min.x, aabb.min.y, aabb.max.z},
        {aabb.min.x, aabb.max.y, aabb.min.z},
        {aabb.min.x, aabb.max.y, aabb.max.z},
        {aabb.max.x, aabb.min.y, aabb.min.z},
        {aabb.max.x, aabb.min.y, aabb.max.z},
        {aabb.max.x, aabb.max.y, aabb.min.z},
        {aabb.max.x, aabb.max.y, aabb.max.z},
    };

    rng3_f32 out;
    out.min = (vec3_f32){+MAX_F32, +MAX_F32, +MAX_F32};
    out.max = (vec3_f32){-MAX_F32, -MAX_F32, -MAX_F32};

    for (int i = 0; i < 8; i++) {
        vec3_f32 p = rt_cpu_transform_point(corners[i], translation, rotation, scale);
        out.min = min_3f32(out.min, p);
        out.max = max_3f32(out.max, p);
    }

    return out;
}
internal rng3_f32 rt_cpu_inv_transform_ray(rng3_f32 ray, vec3_f32 translation, vec4_f32 rotation, vec3_f32 scale) {
    return (rng3_f32){
        .origin=rt_cpu_inv_transform_point(ray.origin, translation, rotation, scale),
        .direction=normalize_3f32(rt_cpu_inv_transform_dir(ray.direction, rotation, scale)),
    };
}