#pragma once

internal void rt_cpu_dump_begin_ray_hit_record(const char* path);
internal void rt_cpu_dump_add_ray_hit_record(const rng3_f32* in_ray, const RT_CPU_HitRecord* in_hit_record, const char* path);
internal void rt_cpu_dump_add_ray_miss_record(const rng3_f32* in_ray, const char* path);