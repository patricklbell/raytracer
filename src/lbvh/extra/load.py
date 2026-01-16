import bpy
import bmesh

BVH_PATH = "/absolute/path/to/tree.bvh"

# --------------------
# AABB helper (returns object)
# --------------------
def aabb_to_mesh(min_pt, max_pt, name, depth):
    mesh = bpy.data.meshes.new(name + "_mesh")
    obj = bpy.data.objects.new(name, mesh)

    bpy.context.collection.objects.link(obj)

    obj.display_type = 'BOUNDS'
    obj.show_in_front = True

    # Depth-based color (blue -> red)
    max_depth = 20.0
    t = min(depth / max_depth, 1.0)
    obj.color = (t, 0.2, 1.0 - t, 1.0)

    bm = bmesh.new()
    bm.verts.new(min_pt)
    bm.verts.new(max_pt)
    bm.to_mesh(mesh)
    bm.free()

    return obj

# --------------------
# BVH parsing
# --------------------
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

    print(parts)
    print("min -> ", parts[2:5])
    print("max -> ", parts[5:8])
    min_pt = tuple(map(float, parts[2:5]))
    max_pt = tuple(map(float, parts[5:8]))

    node = Node(min_pt, max_pt)

    node.left, idx = parse_node(lines, idx)
    node.right, idx = parse_node(lines, idx)

    return node, idx

# --------------------
# Draw BVH with hierarchy
# --------------------
def draw_tree(node, parent_obj=None, depth=0):
    if not node:
        return

    name = f"BVH_Node_Depth_{depth}"
    obj = aabb_to_mesh(node.min, node.max, name, depth)

    if parent_obj:
        obj.parent = parent_obj

    draw_tree(node.left, obj, depth + 1)
    draw_tree(node.right, obj, depth + 1)

# --------------------
# Load & draw
# --------------------
with open(BVH_PATH, "r") as f:
    lines = [l.strip() for l in f if l.strip()]

assert lines[0].startswith("LBVH")

root, _ = parse_node(lines, 1)
draw_tree(root)
