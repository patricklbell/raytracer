#include <stdio.h>

internal void rt_cpu_dump_begin_ray_hit_record(const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f) {
        return;
    }
    fclose(f);
}

internal void rt_cpu_dump_add_ray_hit_record(const rng3_f32* in_ray, const RT_CPU_HitRecord* in_hit_record, const char* path) {
    FILE* f = fopen(path, "ab");
    if (!f) {
        return;
    }

    fwrite(&in_ray->origin, sizeof(in_ray->origin), 1, f);
    fwrite(&in_ray->direction, sizeof(in_ray->direction), 1, f);
    fwrite(&in_hit_record->p, sizeof(in_hit_record->p), 1, f);
    fwrite(&in_hit_record->n, sizeof(in_hit_record->n), 1, f);
    fwrite(&in_hit_record->t, sizeof(in_hit_record->t), 1, f);
    fwrite(&in_hit_record->material.v64[0], sizeof(u64), 1, f);

    fclose(f);
}

internal void rt_cpu_dump_add_ray_miss_record(const rng3_f32* in_ray, const char* path) {
    RT_CPU_HitRecord miss_record = zero_struct;
    miss_record.t = -1.0f;

    rt_cpu_dump_add_ray_hit_record(in_ray, &miss_record, path);
}