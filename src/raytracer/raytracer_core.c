#pragma once

#define rt_hook internal

RT_World* rt_make_world(RT_WorldSettings settings) {
    Arena* arena = arena_alloc();
    RT_World* world = push_array(arena, RT_World, 1);
    world->arena = arena;
    return world;
}

void rt_world_add_sphere(RT_World* world, RT_Sphere sphere) {
    RT_EntityNode* node = push_array(world->arena, RT_EntityNode, 1);
    node->v.type = RT_EntityType_Sphere;
    node->v.sphere = sphere;
    stack_push(world->entities, node);
}

void rt_world_cleanup(RT_World* world) {
    arena_release(world->arena);
}