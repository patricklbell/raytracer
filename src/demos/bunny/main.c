#include "demos/demos_inc.h"
#include "demos/demos_inc.c"

static RT_Handle add_lambertian_billboard_material(RT_World* world, vec3_f32 albedo) {
    RT_Handle mat = rt_world_add_material(world);
    RT_Material* mat_ptr = rt_world_resolve_material(world, mat);
    mat_ptr->type = RT_MaterialType_Lambertian;
    mat_ptr->albedo = albedo;
    mat_ptr->billboard = true;

    return mat;
}

static RT_Handle add_mesh(RT_World* world, const MS_Mesh* ms_mesh, NTString8 name) {
    RT_Handle rt_mesh = rt_world_add_mesh(world);
    RT_Mesh* rt_mesh_ptr = rt_world_resolve_mesh(world, rt_mesh);
    rt_mesh_ptr->vertices = ms_mesh->vertices;
    rt_mesh_ptr->vertices_count = ms_mesh->vertices_count;
    rt_mesh_ptr->indices = ms_mesh->indices;
    rt_mesh_ptr->indices_count = ms_mesh->indices_count;
    rt_mesh_ptr->primitive = ms_mesh->primitive;
    rt_mesh_ptr->attrs = ms_mesh->attrs;
#if BUILD_DEBUG
    rt_mesh_ptr->name = name;
#endif
    return rt_mesh;
}

static void add_instance(RT_World* world, RT_Handle mesh, RT_Handle mat) {
    RT_Handle instance = rt_world_add_instance(world);
    RT_Instance* instance_ptr = rt_world_resolve_instance(world, instance);
    instance_ptr->type = RT_InstanceType_Mesh;
    instance_ptr->material = mat;
    instance_ptr->mesh.handle = mesh;
    instance_ptr->mesh.rotation = make_identity_quat();
    instance_ptr->mesh.scale = make_scale_3f32(1.f);
}

demo_hook void render(const DEMO_Settings* settings) {
    {DeferResource(Temp scratch = scratch_begin(NULL, 0), scratch_end(scratch)) {
        MS_LoadResult bunny_asset = ms_load_obj(
            scratch.arena,
            ntstr8_lit("data/bunny.obj"),
            (MS_LoadSettings){
                .primitive=GEO_Primitive_TRI_LIST,
                .attrs=GEO_VertexAttributes_PN
            }
        );
        Assert(bunny_asset.error.length == 0);

        {DeferResource(RT_World* world = rt_make_world((RT_WorldSettings){}), rt_world_cleanup(world)) {
            RT_TracerSettings tsettings = get_rt_tracer_settings(settings, (DEMO_ExtraTracerSettings){});
            {DeferResource(RT_Handle tracer = rt_make_tracer(tsettings), rt_tracer_cleanup(tracer)) {
                RT_Handle bunny_mesh = add_mesh(world, &bunny_asset.v, ntstr8_lit("bunny"));
                
                rt_tracer_build_blas(tracer, world);

                RT_Handle white = add_lambertian_billboard_material(world, make_scale_3f32(0.73));
                add_instance(world, bunny_mesh, white);

                rt_tracer_build_tlas(tracer, world);

                {DeferResource(Temp scratch = scratch_begin(NULL, 0), scratch_end(scratch)) {
                    int width = settings->width, height = settings->height;
                    vec3_f32* buffer = push_array(scratch.arena, vec3_f32, width*height);
                    
                    RT_CastSettings csettings = get_rt_cast_settings(settings,
                        make_3f32(-0.3, 1.2, 3.5), make_3f32(-0.3, 1.2, 0),
                        (DEMO_ExtraCastSettings){.vfov=DegreesToRad(45)}
                    );
                    rt_tracer_cast(tracer, csettings, buffer, width, height);

                    stbi_write_hdr(settings->out.cstr, width, height, 3, &buffer[0].v[0]);
                }}}
            }}
        }
    }}
}