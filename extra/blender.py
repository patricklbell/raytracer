import bpy
import bmesh
import struct
import numpy as np
from mathutils import Vector
from bpy.props import StringProperty
import math

# ============================================================
# CONFIG
# ============================================================
DRAW_HIT_POINTS = True
HIT_RADIUS = 0.015

# Binary layout (must match C)
RECORD_FMT = "<3f 3f 3f 3f f Q"
RECORD_SIZE = struct.calcsize(RECORD_FMT)

MISS_COLOR = (1.0, 0.0, 0.0, 1.0)   # Red
HIT_COLOR  = (0.0, 1.0, 0.0, 1.0)   # Green
RAY_COLOR  = (0.0, 0.5, 1.0, 1.0)   # Blue
MISS_LENGTH = 2.0                    # Blender units

# ============================================================
# Coordinate system conversion
# Input: (x, y, z) in your system (Y up, X right, Z forward)
# Output: Blender system (X right, Y forward, Z up)
# ============================================================
def to_blender(v):
    """Convert from (Y up, X right, Z forward) to Blender coords (Z up)."""
    x, y, z = v
    # Mapping:
    # X -> X
    # Y -> Z
    # Z -> -Y (so into the screen becomes forward)
    return (x, -z, y)

def to_blender_size(v):
    """Convert from (Y up, X right, Z forward) to Blender coords (Z up)."""
    x, y, z = v
    # Mapping:
    # X -> X
    # Y -> Z
    # Z -> -Y (so into the screen becomes forward)
    return (x, z, y)

# ============================================================
# Scene properties for file picker
# ============================================================
if not hasattr(bpy.types.Scene, "ray_dump_path"):
    bpy.types.Scene.ray_dump_path = StringProperty(
        name="Rays File",
        description="Path to ray binary file",
        subtype='FILE_PATH'
    )

if not hasattr(bpy.types.Scene, "bvh_path"):
    bpy.types.Scene.bvh_path = StringProperty(
        name="BVH File",
        description="Path to BVH text file",
        subtype='FILE_PATH'
    )

# ============================================================
# Binary ray dump loader
# ============================================================
def load_ray_dump(path):
    with open(path, "rb") as f:
        data = f.read()
    count = len(data) // RECORD_SIZE
    if count == 0:
        return None
    mv = memoryview(data)
    it = struct.iter_unpack(RECORD_FMT, mv)
    origin    = np.empty((count, 3), dtype=np.float32)
    direction = np.empty((count, 3), dtype=np.float32)
    hit_p     = np.empty((count, 3), dtype=np.float32)
    t         = np.empty(count, dtype=np.float32)
    for i, r in enumerate(it):
        origin[i]    = r[0:3]
        direction[i] = r[3:6]
        hit_p[i]     = r[6:9]
        t[i]         = r[12]
    return origin, direction, hit_p, t

# ============================================================
# Draw rays (single mesh), separate hits and misses
# ============================================================
def draw_rays(coll, origin, direction, t):
    verts_hit = []
    edges_hit = []
    verts_miss = []
    edges_miss = []

    for i in range(len(origin)):
        o = Vector(to_blender(origin[i]))
        if t[i] > 0.0:  # Hit
            e = o + Vector(to_blender(direction[i])) * t[i]
            idx = len(verts_hit)
            verts_hit.append(o)
            verts_hit.append(e)
            edges_hit.append((idx, idx + 1))
        else:           # Miss
            e = o + Vector(to_blender(direction[i])) * MISS_LENGTH
            idx = len(verts_miss)
            verts_miss.append(o)
            verts_miss.append(e)
            edges_miss.append((idx, idx + 1))

    # --- Hit rays mesh ---
    if verts_hit:
        mesh_hit = bpy.data.meshes.new("Hit")
        mesh_hit.from_pydata(verts_hit, edges_hit, [])
        mesh_hit.update()
        obj_hit = bpy.data.objects.new("Hit", mesh_hit)
        coll.objects.link(obj_hit)
        obj_hit.show_in_front = True
        obj_hit.color = RAY_COLOR
        obj_hit.display_type = 'WIRE'

    # --- Miss rays mesh ---
    if verts_miss:
        mesh_miss = bpy.data.meshes.new("Miss")
        mesh_miss.from_pydata(verts_miss, edges_miss, [])
        mesh_miss.update()
        obj_miss = bpy.data.objects.new("Miss", mesh_miss)
        coll.objects.link(obj_miss)
        obj_miss.show_in_front = True
        obj_miss.color = MISS_COLOR
        obj_miss.display_type = 'WIRE'

# ============================================================
# Draw hit points (batched)
# ============================================================
def draw_hit_points(coll, hit_p):
    verts = [Vector(to_blender(p)) for p in hit_p]  # --- TRANSFORM ---
    mesh = bpy.data.meshes.new("Points")
    mesh.from_pydata(verts, [], [])
    mesh.update()
    obj = bpy.data.objects.new("Points", mesh)
    coll.objects.link(obj)
    obj.show_in_front = True
    obj.color = HIT_COLOR
    obj.display_type = 'WIRE'
    return obj

# ============================================================
# BVH helpers (using instanced empties to avoid clipping)
# ============================================================
class Node:
    def __init__(self, min_pt, max_pt, node_id):
        self.id = node_id
        self.min = min_pt
        self.max = max_pt
        self.left = None
        self.right = None

def draw_bvh(path, use_collections=True, min_size=0.0001):
    """
    Stream-load a BVH binary file and draw in Blender.
    Supports:
        - Flat mode: one mesh per depth layer
        - Collection mode: subtree collections for toggling
    """

    base_verts = [
        (-0.5, -0.5, -0.5),
        ( 0.5, -0.5, -0.5),
        ( 0.5,  0.5, -0.5),
        (-0.5,  0.5, -0.5),
        (-0.5, -0.5,  0.5),
        ( 0.5, -0.5,  0.5),
        ( 0.5,  0.5,  0.5),
        (-0.5,  0.5,  0.5),
    ]

    base_edges = [
        (0,1),(1,2),(2,3),(3,0),
        (4,5),(5,6),(6,7),(7,4),
        (0,4),(1,5),(2,6),(3,7),
    ]

    # Blender root collection
    scene_coll = bpy.context.scene.collection
    root_coll = bpy.data.collections.new("BVH")
    scene_coll.children.link(root_coll)

    # Shared wire mesh for collection mode
    def get_shared_mesh():
        name = "BVH_WireCube"
        mesh = bpy.data.meshes.get(name)
        if mesh:
            return mesh
        mesh = bpy.data.meshes.new(name)
        mesh.from_pydata(base_verts, base_edges, [])
        mesh.update()
        return mesh

    shared_mesh = get_shared_mesh()

    def clamp_size(size):
        return Vector(tuple(max(x, min_size) for x in size))
    
    def sanitize_point(p, max_abs=1e6):
        return tuple(
            max(min(coord, max_abs), -max_abs) if not math.isnan(coord) else 0.0
            for coord in p
        )

    # ----------------------------
    # Flat mode: one object per depth
    # ----------------------------
    if not use_collections:
        layer_data = {}  # depth -> (verts, edges)
        max_depth = 0

        # First pass: compute max depth for progress bar
        def compute_max_depth(f, depth=0):
            nonlocal max_depth
            b = f.read(1)
            if not b:
                return
            if b == b'\x00':
                return
            f.read(32)  # id+min+max
            if depth > max_depth:
                max_depth = depth
            compute_max_depth(f, depth+1)
            compute_max_depth(f, depth+1)

        # Open file twice: first to get max depth, second to stream draw
        with open(path, 'rb') as f:
            compute_max_depth(f)
        wm = bpy.context.window_manager
        wm.progress_begin(0, max_depth+1)

        # Streaming parse and add to per-layer mesh
        def add_cube_to_layer(min_pt, max_pt, depth):
            min = Vector(min_pt)
            max = Vector(max_pt)
            center = (min + max) * 0.5
            size = max - min
            verts, edges = layer_data.setdefault(depth, ([], []))
            v_offset = len(verts)
            for v in base_verts:
                verts.append(to_blender((
                    center.x + v[0]*size.x,
                    center.y + v[1]*size.y,
                    center.z + v[2]*size.z
                )))
            for e in base_edges:
                edges.append((e[0]+v_offset, e[1]+v_offset))

        def parse_node_stream(f, depth=0):
            b = f.read(1)
            if not b:
                return
            if b == b'\x00':
                return
            
            node_id = struct.unpack('<Q', f.read(8))[0]
            min_pt = struct.unpack('<3f', f.read(12))
            max_pt = struct.unpack('<3f', f.read(12))

            add_cube_to_layer(min_pt, max_pt, depth)
            parse_node_stream(f, depth+1)
            parse_node_stream(f, depth+1)

        with open(path, 'rb') as f:
            parse_node_stream(f)

        # Create one object per depth layer
        for depth, (verts, edges) in layer_data.items():
            mesh = bpy.data.meshes.new(f"Layer {depth}")
            mesh.from_pydata(verts, edges, [])
            mesh.update()

            obj = bpy.data.objects.new(f"Layer {depth}", mesh)
            obj.display_type = 'WIRE'
            obj.show_in_front = True
            obj.hide_render = True
            obj.hide_select = True
            root_coll.objects.link(obj)

            wm.progress_update(depth+1)

        wm.progress_end()
        return

    # ----------------------------
    # Collection mode: subtree toggling
    # ----------------------------
    def parse_node_collection(f, parent_collection, depth=0):
        b = f.read(1)
        if not b:
            return
        if b == b'\x00':
            return
        
        node_id = struct.unpack('<Q', f.read(8))[0]
        min_pt = struct.unpack('<3f', f.read(12))
        max_pt = struct.unpack('<3f', f.read(12))

        # Create collection if branching
        left_peek = f.peek(1)[:1]
        right_peek = None
        left = None
        right = None
        if left_peek != b'\x00':
            current_coll = bpy.data.collections.new(f"BVH_Node_{node_id}")
            parent_collection.children.link(current_coll)
        else:
            current_coll = parent_collection

        # Draw node
        min_v = Vector(min_pt)
        max_v = Vector(max_pt)
        center = to_blender((min_v + max_v) * 0.5)
        size = to_blender_size(max_v - min_v)
        obj = bpy.data.objects.new(f"BVH_Node_{node_id}", shared_mesh)
        obj.location = center
        obj.scale = size
        obj.display_type = 'WIRE'
        obj.show_in_front = True
        obj.hide_render = True
        obj.hide_select = True
        current_coll.objects.link(obj)

        # Recurse
        parse_node_collection(f, current_coll, depth+1)
        parse_node_collection(f, current_coll, depth+1)

    # Open file and parse into collections
    with open(path, 'rb') as f:
        parse_node_collection(f, root_coll, 0)

# ============================================================
# Operators
# ============================================================
class RAYDUMP_OT_load(bpy.types.Operator):
    bl_idname = "raydump.load"
    bl_label = "Load Rays"

    def execute(self, context):
        scene = context.scene
        path = scene.ray_dump_path
        if not path:
            self.report({'ERROR'}, "No ray dump path set")
            return {'CANCELLED'}
        data = load_ray_dump(path)
        if not data:
            self.report({'ERROR'}, "Failed to load ray dump")
            return {'CANCELLED'}
        origin, direction, hit_p, t = data

        scene_coll = bpy.context.scene.collection
        root_coll = bpy.data.collections.new("Rays")
        scene_coll.children.link(root_coll)
        
        draw_rays(root_coll, origin, direction, t)
        if DRAW_HIT_POINTS:
            draw_hit_points(root_coll, hit_p)
        self.report({'INFO'}, f"Loaded {len(origin)} rays")
        return {'FINISHED'}

class BVH_OT_load(bpy.types.Operator):
    bl_idname = "bvh.load"
    bl_label = "Load BVH"

    def execute(self, context):
        scene = context.scene
        path = scene.bvh_path
        if not path:
            self.report({'ERROR'}, "No BVH path set")
            return {'CANCELLED'}
        draw_bvh(path, scene.bvh_use_collections)
        self.report({'INFO'}, "BVH loaded")
        return {'FINISHED'}

# ============================================================
# UI Panel
# ============================================================
class RAYDUMP_PT_panel(bpy.types.Panel):
    bl_label = "RayDump Loader"
    bl_idname = "RAYDUMP_PT_panel"
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_category = 'RayDump'

    def draw(self, context):
        layout = self.layout
        scene = context.scene
        layout.prop(scene, "ray_dump_path")
        layout.operator("raydump.load", text="Load Rays")

        layout.separator()

        layout.prop(scene, "bvh_path")
        layout.prop(scene, "bvh_use_collections")
        layout.operator("bvh.load", text="Load BVH")

# ============================================================
# Register
# ============================================================
classes = [RAYDUMP_OT_load, BVH_OT_load, RAYDUMP_PT_panel]

def register():
    for cls in classes:
        bpy.utils.register_class(cls)

    bpy.types.Scene.bvh_use_collections = bpy.props.BoolProperty(
        name="BVH Collections",
        description="Create collections per BVH subtree (slower, allows subtree toggling)",
        default=True
    )

def unregister():
    for cls in reversed(classes):
        bpy.utils.unregister_class(cls)
    if hasattr(bpy.types.Scene, "ray_dump_path"):
        del bpy.types.Scene.ray_dump_path
    if hasattr(bpy.types.Scene, "bvh_path"):
        del bpy.types.Scene.bvh_path

    del bpy.types.Scene.bvh_use_collections

if __name__ == "__main__":
    register()
