import bpy
import bmesh
import struct
import numpy as np
from mathutils import Vector
from bpy.props import StringProperty

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
def draw_rays(origin, direction, t):
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
        mesh_hit = bpy.data.meshes.new("RayDump_Rays_Hit")
        mesh_hit.from_pydata(verts_hit, edges_hit, [])
        mesh_hit.update()
        obj_hit = bpy.data.objects.new("RayDump_Rays_Hit", mesh_hit)
        bpy.context.collection.objects.link(obj_hit)
        obj_hit.show_in_front = True
        obj_hit.color = RAY_COLOR
        obj_hit.display_type = 'WIRE'

    # --- Miss rays mesh ---
    if verts_miss:
        mesh_miss = bpy.data.meshes.new("RayDump_Rays_Miss")
        mesh_miss.from_pydata(verts_miss, edges_miss, [])
        mesh_miss.update()
        obj_miss = bpy.data.objects.new("RayDump_Rays_Miss", mesh_miss)
        bpy.context.collection.objects.link(obj_miss)
        obj_miss.show_in_front = True
        obj_miss.color = MISS_COLOR
        obj_miss.display_type = 'WIRE'

# ============================================================
# Draw hit points (batched)
# ============================================================
def draw_hit_points(hit_p):
    verts = [Vector(to_blender(p)) for p in hit_p]  # --- TRANSFORM ---
    mesh = bpy.data.meshes.new("RayDump_Hits")
    mesh.from_pydata(verts, [], [])
    mesh.update()
    obj = bpy.data.objects.new("RayDump_Hits", mesh)
    bpy.context.collection.objects.link(obj)
    obj.show_in_front = True
    obj.color = HIT_COLOR
    obj.display_type = 'WIRE'
    return obj

# ============================================================
# BVH helpers (using instanced empties to avoid clipping)
# ============================================================
class Node:
    def __init__(self, min_pt, max_pt):
        self.min = min_pt
        self.max = max_pt
        self.left = None
        self.right = None

def parse_node(lines, idx):
    line = lines[idx]
    idx += 1
    if line == "NULL":
        return None, idx
    parts = line.split()
    assert parts[0] == "NODE"
    min_pt = tuple(map(float, parts[2:5]))
    max_pt = tuple(map(float, parts[5:8]))
    node = Node(min_pt, max_pt)
    node.left, idx = parse_node(lines, idx)
    node.right, idx = parse_node(lines, idx)
    return node, idx

def create_wire_cube_solid(min_pt, max_pt, depth=0, name="Cube", inflation=0.001):
    """
    Create a cube mesh from absolute coordinates (min_pt, max_pt) with tiny inflation per depth.
    Apply Wireframe modifier to show as wireframe.
    """
    # Inflate slightly to reduce Z-fighting
    min_pt = Vector(min_pt) - Vector((depth*inflation, depth*inflation, depth*inflation))
    max_pt = Vector(max_pt) + Vector((depth*inflation, depth*inflation, depth*inflation))
    
    x0, y0, z0 = min_pt
    x1, y1, z1 = max_pt

    # Vertices of the cube
    verts = [
        (x0, y0, z0),  # 0
        (x1, y0, z0),  # 1
        (x1, y1, z0),  # 2
        (x0, y1, z0),  # 3
        (x0, y0, z1),  # 4
        (x1, y0, z1),  # 5
        (x1, y1, z1),  # 6
        (x0, y1, z1),  # 7
    ]

    # Faces of the cube (Blender needs quads)
    faces = [
        (0, 1, 2, 3),  # bottom
        (4, 5, 6, 7),  # top
        (0, 1, 5, 4),  # front
        (1, 2, 6, 5),  # right
        (2, 3, 7, 6),  # back
        (3, 0, 4, 7),  # left
    ]
    
    # Create mesh and object
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(verts, [], faces)  # edges can be empty
    mesh.update()
    
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    
    # Wireframe modifier
    mod = obj.modifiers.new("Wireframe", type='WIREFRAME')
    mod.thickness = 0.005
    mod.use_replace = True
    
    obj.show_in_front = True
    
    # Optional material/color
    mat = bpy.data.materials.new(name + "_Mat")
    mat.diffuse_color = (0.0, 0.5, 1.0, 1.0)
    obj.data.materials.append(mat)
    
    return obj

def draw_tree(node, parent_coll=None, depth=0):
    if not node:
        return

    # --- Create solid cube with wireframe ---
    cube_obj = create_wire_cube_solid(to_blender(node.min), to_blender(node.max), depth, name=f"BVH_Node_{depth}")

    # --- Create collection for this node ---
    coll_name = f"BVH_Node_Collection_{depth}"
    coll = bpy.data.collections.new(coll_name)
    coll.objects.link(cube_obj)

    # Link to parent collection or scene
    if parent_coll:
        parent_coll.children.link(coll)
    else:
        bpy.context.scene.collection.children.link(coll)

    # Recursive draw
    draw_tree(node.left, coll, depth + 1)
    draw_tree(node.right, coll, depth + 1)

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
        draw_rays(origin, direction, t)
        if DRAW_HIT_POINTS:
            draw_hit_points(hit_p)
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
        with open(path, "r") as f:
            lines = [l.strip() for l in f if l.strip()]
        if not lines[0].startswith("LBVH"):
            self.report({'ERROR'}, "BVH file must start with LBVH")
            return {'CANCELLED'}
        root, _ = parse_node(lines, 1)
        draw_tree(root)
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
        layout.prop(scene, "bvh_path")
        layout.operator("bvh.load", text="Load BVH")

# ============================================================
# Register
# ============================================================
classes = [RAYDUMP_OT_load, BVH_OT_load, RAYDUMP_PT_panel]

def register():
    for cls in classes:
        bpy.utils.register_class(cls)

def unregister():
    for cls in reversed(classes):
        bpy.utils.unregister_class(cls)
    if hasattr(bpy.types.Scene, "ray_dump_path"):
        del bpy.types.Scene.ray_dump_path
    if hasattr(bpy.types.Scene, "bvh_path"):
        del bpy.types.Scene.bvh_path

if __name__ == "__main__":
    register()
