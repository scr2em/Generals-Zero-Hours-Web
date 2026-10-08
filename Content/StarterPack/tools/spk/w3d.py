"""W3D model writer (meshes, hierarchies, HLODs, animations) plus a validating reader.

Chunk layouts follow ``w3d_file.h`` and the engine's loaders
(``MeshModelClass::Load_W3D`` / ``HTreeClass::Load_W3D`` / ``HLodDefClass::Load_W3D`` /
``HRawAnimClass::Load_W3D``). All structures are little endian and tightly packed.

Typical use::

    mesh = Mesh("BODY", texture="iwhq.tga")
    mesh.add_box((0, 0, 1), (2, 2, 1))
    model = Model("IWHQ")                      # one root bone
    model.add_mesh(mesh)                       # attached to the root bone
    open("IWHQ.w3d", "wb").write(model.to_bytes())

Meshes whose name starts with ``HOUSECOLOR`` are recoloured with the owning
player's colour by ``W3DAssetManager::Recolor_Mesh``; textures whose name starts
with ``ZHC`` are hue shifted. The pack uses the former.

Coordinate system: X right, Y forward, Z up (the engine's world space).
"""

import math
import struct

from .util import fixed_string

# ---- chunk ids (w3d_file.h) --------------------------------------------------
W3D_CHUNK_MESH = 0x00
W3D_CHUNK_VERTICES = 0x02
W3D_CHUNK_VERTEX_NORMALS = 0x03
W3D_CHUNK_MESH_USER_TEXT = 0x0C
W3D_CHUNK_MESH_HEADER3 = 0x1F
W3D_CHUNK_TRIANGLES = 0x20
W3D_CHUNK_MATERIAL_INFO = 0x28
W3D_CHUNK_SHADERS = 0x29
W3D_CHUNK_VERTEX_MATERIALS = 0x2A
W3D_CHUNK_VERTEX_MATERIAL = 0x2B
W3D_CHUNK_VERTEX_MATERIAL_NAME = 0x2C
W3D_CHUNK_VERTEX_MATERIAL_INFO = 0x2D
W3D_CHUNK_TEXTURES = 0x30
W3D_CHUNK_TEXTURE = 0x31
W3D_CHUNK_TEXTURE_NAME = 0x32
W3D_CHUNK_TEXTURE_INFO = 0x33
W3D_CHUNK_MATERIAL_PASS = 0x38
W3D_CHUNK_VERTEX_MATERIAL_IDS = 0x39
W3D_CHUNK_SHADER_IDS = 0x3A
W3D_CHUNK_DCG = 0x3B
W3D_CHUNK_TEXTURE_STAGE = 0x48
W3D_CHUNK_TEXTURE_IDS = 0x49
W3D_CHUNK_STAGE_TEXCOORDS = 0x4A
W3D_CHUNK_HIERARCHY = 0x100
W3D_CHUNK_HIERARCHY_HEADER = 0x101
W3D_CHUNK_PIVOTS = 0x102
W3D_CHUNK_ANIMATION = 0x200
W3D_CHUNK_ANIMATION_HEADER = 0x201
W3D_CHUNK_ANIMATION_CHANNEL = 0x202
W3D_CHUNK_BIT_CHANNEL = 0x203
W3D_CHUNK_HLOD = 0x700
W3D_CHUNK_HLOD_HEADER = 0x701
W3D_CHUNK_HLOD_LOD_ARRAY = 0x702
W3D_CHUNK_HLOD_SUB_OBJECT_ARRAY_HEADER = 0x703
W3D_CHUNK_HLOD_SUB_OBJECT = 0x704
W3D_CHUNK_BOX = 0x740

SUB_CHUNK_FLAG = 0x80000000

W3D_MESH_FLAG_COLLISION_TYPE_PHYSICAL = 0x10
W3D_MESH_FLAG_COLLISION_TYPE_PROJECTILE = 0x20
W3D_MESH_FLAG_COLLISION_TYPE_VIS = 0x40
W3D_MESH_FLAG_COLLISION_TYPE_CAMERA = 0x80
W3D_MESH_FLAG_COLLISION_TYPE_VEHICLE = 0x100
W3D_MESH_FLAG_HIDDEN = 0x1000
W3D_MESH_FLAG_TWO_SIDED = 0x2000
W3D_MESH_FLAG_CAST_SHADOW = 0x8000

ANIM_CHANNEL_X, ANIM_CHANNEL_Y, ANIM_CHANNEL_Z = 0, 1, 2
ANIM_CHANNEL_Q = 6

W3D_MESH_VERSION = (4 << 16) | 2
W3D_HTREE_VERSION = (4 << 16) | 1
W3D_HANIM_VERSION = (4 << 16) | 1
W3D_HLOD_VERSION = (1 << 16) | 0

NO_PARENT = 0xFFFFFFFF
NAME_LEN = 16


def chunk(chunk_type, payload, container=False):
    """One chunk: type, size (MSB set when the payload holds sub chunks), payload."""
    size = len(payload)
    if container:
        size |= SUB_CHUNK_FLAG
    return struct.pack("<II", chunk_type, size) + payload


# ---- shaders -----------------------------------------------------------------
class Shader:
    """A W3dShaderStruct (16 bytes). See the W3DSHADER_* enums in w3d_file.h."""

    FIELDS = ("depth_compare", "depth_mask", "color_mask", "dest_blend", "fog_func",
              "pri_gradient", "sec_gradient", "src_blend", "texturing", "detail_color",
              "detail_alpha", "preset", "alpha_test", "post_detail_color", "post_detail_alpha", "pad")

    def __init__(self, depth_compare=3, depth_mask=1, dest_blend=0, pri_gradient=1, sec_gradient=0,
                 src_blend=1, texturing=1, detail_color=0, detail_alpha=0, alpha_test=0):
        self.depth_compare = depth_compare
        self.depth_mask = depth_mask
        self.dest_blend = dest_blend
        self.pri_gradient = pri_gradient
        self.sec_gradient = sec_gradient
        self.src_blend = src_blend
        self.texturing = texturing
        self.detail_color = detail_color
        self.detail_alpha = detail_alpha
        self.alpha_test = alpha_test

    def pack(self):
        return struct.pack("<16B", self.depth_compare, self.depth_mask, 0, self.dest_blend, 0,
                           self.pri_gradient, self.sec_gradient, self.src_blend, self.texturing,
                           self.detail_color, self.detail_alpha, 0, self.alpha_test, 0, 0, 0)

    @staticmethod
    def opaque(texturing=True):
        return Shader(texturing=1 if texturing else 0)

    @staticmethod
    def alpha_blend(depth_write=False):
        return Shader(depth_mask=1 if depth_write else 0, src_blend=2, dest_blend=5)

    @staticmethod
    def alpha_test():
        return Shader(alpha_test=1, src_blend=2, dest_blend=5)

    @staticmethod
    def additive():
        return Shader(depth_mask=0, src_blend=1, dest_blend=1)


class VertexMaterial:
    """A W3dVertexMaterialStruct (names are optional)."""

    def __init__(self, name="Mat", ambient=(255, 255, 255), diffuse=(255, 255, 255), specular=(0, 0, 0),
                 emissive=(0, 0, 0), shininess=1.0, opacity=1.0, translucency=0.0):
        self.name = name
        self.ambient, self.diffuse, self.specular, self.emissive = ambient, diffuse, specular, emissive
        self.shininess, self.opacity, self.translucency = shininess, opacity, translucency

    def pack(self):
        rgb = lambda c: struct.pack("<4B", c[0], c[1], c[2], 0)
        return (struct.pack("<I", 0) + rgb(self.ambient) + rgb(self.diffuse) + rgb(self.specular)
                + rgb(self.emissive) + struct.pack("<3f", self.shininess, self.opacity, self.translucency))


# ---- geometry ------------------------------------------------------------------
def _sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _norm(v):
    l = math.sqrt(v[0] ** 2 + v[1] ** 2 + v[2] ** 2)
    if l < 1e-12:
        return (0.0, 0.0, 1.0)
    return (v[0] / l, v[1] / l, v[2] / l)


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


class Mesh:
    """An indexed triangle mesh with one material, built from primitives.

    ``name`` is the short mesh name; the full W3D name is ``<container>.<name>``
    once the mesh is attached to a :class:`Model`.
    """

    def __init__(self, name, texture=None, shader=None, material=None, attributes=None,
                 vertex_colors=None):
        self.name = name
        self.texture = texture
        self.shader = shader or Shader.opaque(texturing=texture is not None)
        self.material = material or VertexMaterial(name=name)
        self.attributes = (W3D_MESH_FLAG_COLLISION_TYPE_PHYSICAL | W3D_MESH_FLAG_COLLISION_TYPE_PROJECTILE
                           | W3D_MESH_FLAG_COLLISION_TYPE_VIS | W3D_MESH_FLAG_CAST_SHADOW) if attributes is None else attributes
        self.positions = []
        self.normals = []
        self.uvs = []
        self.colors = []  # optional per-vertex RGBA, used when any vertex has one
        self.triangles = []
        self.user_text = ""

    # -- primitive construction --------------------------------------------------
    def add_vertex(self, position, normal, uv=(0.0, 0.0), color=None):
        self.positions.append(tuple(float(c) for c in position))
        self.normals.append(_norm(normal))
        self.uvs.append((float(uv[0]), float(uv[1])))
        self.colors.append(color)
        return len(self.positions) - 1

    def add_triangle(self, a, b, c):
        self.triangles.append((a, b, c))

    def add_quad(self, p0, p1, p2, p3, uv=((0, 0), (1, 0), (1, 1), (0, 1)), color=None):
        """A flat quad with counter-clockwise winding p0..p3 (viewed from the front)."""
        n = _norm(_cross(_sub(p1, p0), _sub(p3, p0)))
        i = [self.add_vertex(p, n, u, color) for p, u in zip((p0, p1, p2, p3), uv)]
        self.add_triangle(i[0], i[1], i[2])
        self.add_triangle(i[0], i[2], i[3])

    def add_polygon(self, points, uvs=None, color=None):
        """A flat convex polygon (counter-clockwise), triangulated as a fan."""
        n = _norm(_cross(_sub(points[1], points[0]), _sub(points[2], points[0])))
        idx = [self.add_vertex(p, n, uvs[k] if uvs else (0.0, 0.0), color) for k, p in enumerate(points)]
        for k in range(1, len(points) - 1):
            self.add_triangle(idx[0], idx[k], idx[k + 1])

    def add_box(self, center, size, uv_scale=1.0, color=None, taper=None):
        """Axis aligned box. ``size`` is the full extent (x, y, z).

        ``taper=(sx, sy)`` scales the top face about its centre (a frustum/roof shape).
        """
        cx, cy, cz = center
        hx, hy, hz = size[0] / 2.0, size[1] / 2.0, size[2] / 2.0
        tx, ty = (1.0, 1.0) if taper is None else taper
        b = [(cx - hx, cy - hy, cz - hz), (cx + hx, cy - hy, cz - hz),
             (cx + hx, cy + hy, cz - hz), (cx - hx, cy + hy, cz - hz)]
        t = [(cx - hx * tx, cy - hy * ty, cz + hz), (cx + hx * tx, cy - hy * ty, cz + hz),
             (cx + hx * tx, cy + hy * ty, cz + hz), (cx - hx * tx, cy + hy * ty, cz + hz)]
        u = uv_scale
        quad_uv = ((0, 1 * u), (1 * u, 1 * u), (1 * u, 0), (0, 0))
        self.add_quad(b[3], b[2], b[1], b[0], color=color)               # bottom (faces down)
        self.add_quad(t[0], t[1], t[2], t[3], color=color)               # top
        self.add_quad(b[0], b[1], t[1], t[0], quad_uv, color)            # front (-y)
        self.add_quad(b[1], b[2], t[2], t[1], quad_uv, color)            # right (+x)
        self.add_quad(b[2], b[3], t[3], t[2], quad_uv, color)            # back (+y)
        self.add_quad(b[3], b[0], t[0], t[3], quad_uv, color)            # left (-x)

    def add_prism(self, center, radius, height, sides=8, radius_top=None, color=None, cap_top=True,
                  cap_bottom=True, smooth=True):
        """A vertical (Z axis) cylinder/cone/frustum with ``sides`` facets."""
        cx, cy, cz = center
        rt = radius if radius_top is None else radius_top
        z0, z1 = cz - height / 2.0, cz + height / 2.0
        ring0 = [(cx + radius * math.cos(2 * math.pi * k / sides), cy + radius * math.sin(2 * math.pi * k / sides), z0)
                 for k in range(sides)]
        ring1 = [(cx + rt * math.cos(2 * math.pi * k / sides), cy + rt * math.sin(2 * math.pi * k / sides), z1)
                 for k in range(sides)]
        slope = (radius - rt) / max(1e-6, height)
        for k in range(sides):
            k2 = (k + 1) % sides
            a0, a1, b0, b1 = ring0[k], ring0[k2], ring1[k], ring1[k2]
            if smooth:
                na = _norm((math.cos(2 * math.pi * k / sides), math.sin(2 * math.pi * k / sides), slope))
                nb = _norm((math.cos(2 * math.pi * k2 / sides), math.sin(2 * math.pi * k2 / sides), slope))
            else:
                mid = 2 * math.pi * (k + 0.5) / sides
                na = nb = _norm((math.cos(mid), math.sin(mid), slope))
            u0, u1 = k / sides, (k + 1) / sides
            i0 = self.add_vertex(a0, na, (u0, 1), color)
            i1 = self.add_vertex(a1, nb, (u1, 1), color)
            i2 = self.add_vertex(b1, nb, (u1, 0), color)
            i3 = self.add_vertex(b0, na, (u0, 0), color)
            self.add_triangle(i0, i1, i2)
            self.add_triangle(i0, i2, i3)
        if cap_top and rt > 1e-6:
            self.add_polygon([(cx + rt * math.cos(2 * math.pi * k / sides) , cy + rt * math.sin(2 * math.pi * k / sides), z1)
                              for k in range(sides)],
                             [(0.5 + 0.5 * math.cos(2 * math.pi * k / sides), 0.5 + 0.5 * math.sin(2 * math.pi * k / sides))
                              for k in range(sides)], color)
        if cap_bottom and radius > 1e-6:
            self.add_polygon([(cx + radius * math.cos(-2 * math.pi * k / sides), cy + radius * math.sin(-2 * math.pi * k / sides), z0)
                              for k in range(sides)],
                             [(0.5 + 0.5 * math.cos(-2 * math.pi * k / sides), 0.5 + 0.5 * math.sin(-2 * math.pi * k / sides))
                              for k in range(sides)], color)

    def add_sphere(self, center, radius, segments=8, rings=5, color=None, squash=(1.0, 1.0, 1.0)):
        """A low-poly UV sphere (pole to pole around Z)."""
        cx, cy, cz = center
        grid = []
        for r in range(rings + 1):
            phi = math.pi * r / rings
            row = []
            for s in range(segments + 1):
                th = 2 * math.pi * s / segments
                n = (math.sin(phi) * math.cos(th), math.sin(phi) * math.sin(th), math.cos(phi))
                p = (cx + radius * squash[0] * n[0], cy + radius * squash[1] * n[1], cz + radius * squash[2] * n[2])
                row.append(self.add_vertex(p, n, (s / segments, r / rings), color))
            grid.append(row)
        # a=top-left b=top-right c=bottom-right d=bottom-left as seen from outside;
        # counter-clockwise winding is a-d-c / a-c-b. The pole rows collapse one triangle.
        for r in range(rings):
            for s in range(segments):
                a, b = grid[r][s], grid[r][s + 1]
                c, d = grid[r + 1][s + 1], grid[r + 1][s]
                if r != rings - 1:
                    self.add_triangle(a, d, c)
                if r != 0:
                    self.add_triangle(a, c, b)

    def add_wedge(self, center, size, color=None):
        """A ramp/wedge: full height at the back (+y), zero height at the front (-y)."""
        cx, cy, cz = center
        hx, hy, hz = size[0] / 2.0, size[1] / 2.0, size[2] / 2.0
        f0, f1 = (cx - hx, cy - hy, cz - hz), (cx + hx, cy - hy, cz - hz)
        b0, b1 = (cx - hx, cy + hy, cz - hz), (cx + hx, cy + hy, cz - hz)
        t0, t1 = (cx - hx, cy + hy, cz + hz), (cx + hx, cy + hy, cz + hz)
        self.add_quad(b0, b1, f1, f0, color=color)                 # bottom
        self.add_quad(f0, f1, t1, t0, color=color)                 # slope
        self.add_quad(b1, b0, t0, t1, color=color)                 # back
        self.add_polygon([f0, t0, b0], color=color)                # left side
        self.add_polygon([f1, b1, t1], color=color)                # right side

    def transform(self, scale=(1, 1, 1), rotate_z=0.0, translate=(0, 0, 0)):
        """Apply scale, then a rotation about Z (radians), then a translation."""
        c, s = math.cos(rotate_z), math.sin(rotate_z)
        for i, p in enumerate(self.positions):
            x, y, z = p[0] * scale[0], p[1] * scale[1], p[2] * scale[2]
            self.positions[i] = (x * c - y * s + translate[0], x * s + y * c + translate[1], z + translate[2])
            n = self.normals[i]
            self.normals[i] = _norm((n[0] * c - n[1] * s, n[0] * s + n[1] * c, n[2]))
        return self

    def append(self, other, translate=(0, 0, 0), rotate_z=0.0, scale=(1, 1, 1)):
        """Copy another mesh's geometry into this one (with a transform)."""
        c, s = math.cos(rotate_z), math.sin(rotate_z)
        base = len(self.positions)
        for p, n, uv, col in zip(other.positions, other.normals, other.uvs, other.colors):
            x, y, z = p[0] * scale[0], p[1] * scale[1], p[2] * scale[2]
            q = (x * c - y * s + translate[0], x * s + y * c + translate[1], z + translate[2])
            m = _norm((n[0] * c - n[1] * s, n[0] * s + n[1] * c, n[2]))
            self.positions.append(q)
            self.normals.append(m)
            self.uvs.append(uv)
            self.colors.append(col)
        for a, b, cc in other.triangles:
            self.triangles.append((a + base, b + base, cc + base))

    # -- serialisation -------------------------------------------------------------
    def bounds(self):
        xs = [p[0] for p in self.positions]
        ys = [p[1] for p in self.positions]
        zs = [p[2] for p in self.positions]
        lo = (min(xs), min(ys), min(zs))
        hi = (max(xs), max(ys), max(zs))
        center = tuple((lo[i] + hi[i]) / 2.0 for i in range(3))
        radius = max(math.sqrt(sum((p[i] - center[i]) ** 2 for i in range(3))) for p in self.positions)
        return lo, hi, center, radius

    def to_chunk(self, container_name):
        """The ``W3D_CHUNK_MESH`` chunk, named ``<container_name>.<name>`` (or just the name)."""
        if not self.triangles:
            raise ValueError("mesh %s has no triangles" % self.name)
        full = ("%s.%s" % (container_name, self.name)) if container_name else self.name
        if len(full) > 31:
            raise ValueError("mesh name %r too long" % full)
        lo, hi, center, radius = self.bounds()
        has_color = any(c is not None for c in self.colors)

        header = struct.pack("<II", W3D_MESH_VERSION, self.attributes)
        header += fixed_string(self.name, NAME_LEN) + fixed_string(container_name or "", NAME_LEN)
        header += struct.pack("<IIIIiII", len(self.triangles), len(self.positions), 0, 0, 0, 0, 0)
        header += struct.pack("<II", 0x1 | 0x2 | 0x4 | (0x8 if has_color else 0), 0x1)
        header += struct.pack("<3f3f3ff", *lo, *hi, *center, radius)
        assert len(header) == 116, len(header)

        body = bytearray()
        body += chunk(W3D_CHUNK_MESH_HEADER3, header)
        if self.user_text:
            body += chunk(W3D_CHUNK_MESH_USER_TEXT, self.user_text.encode("ascii") + b"\0")
        tri = bytearray()
        for a, b, c in self.triangles:
            p0, p1, p2 = self.positions[a], self.positions[b], self.positions[c]
            n = _norm(_cross(_sub(p1, p0), _sub(p2, p0)))
            tri += struct.pack("<IIII3ff", a, b, c, 0, n[0], n[1], n[2], _dot(p0, n))
        body += chunk(W3D_CHUNK_TRIANGLES, bytes(tri))
        body += chunk(W3D_CHUNK_VERTICES, b"".join(struct.pack("<3f", *p) for p in self.positions))
        body += chunk(W3D_CHUNK_VERTEX_NORMALS, b"".join(struct.pack("<3f", *n) for n in self.normals))

        textured = self.texture is not None
        body += chunk(W3D_CHUNK_MATERIAL_INFO, struct.pack("<IIII", 1, 1, 1, 1 if textured else 0))
        body += chunk(W3D_CHUNK_SHADERS, self.shader.pack())
        vm = (chunk(W3D_CHUNK_VERTEX_MATERIAL_NAME, self.material.name.encode("ascii") + b"\0")
              + chunk(W3D_CHUNK_VERTEX_MATERIAL_INFO, self.material.pack()))
        body += chunk(W3D_CHUNK_VERTEX_MATERIALS, chunk(W3D_CHUNK_VERTEX_MATERIAL, vm, container=True), container=True)
        if textured:
            tex = (chunk(W3D_CHUNK_TEXTURE_NAME, self.texture.encode("ascii") + b"\0")
                   + chunk(W3D_CHUNK_TEXTURE_INFO, struct.pack("<HHIf", 0, 0, 1, 0.0)))
            body += chunk(W3D_CHUNK_TEXTURES, chunk(W3D_CHUNK_TEXTURE, tex, container=True), container=True)
        passes = chunk(W3D_CHUNK_VERTEX_MATERIAL_IDS, struct.pack("<I", 0))
        passes += chunk(W3D_CHUNK_SHADER_IDS, struct.pack("<I", 0))
        if has_color:
            passes += chunk(W3D_CHUNK_DCG, b"".join(struct.pack("<4B", *(c or (255, 255, 255, 255))) for c in
                                                     [(col if col is None or len(col) == 4 else tuple(col) + (255,)) for col in self.colors]))
        if textured:
            stage = chunk(W3D_CHUNK_TEXTURE_IDS, struct.pack("<I", 0))
            stage += chunk(W3D_CHUNK_STAGE_TEXCOORDS, b"".join(struct.pack("<2f", u, v) for u, v in self.uvs))
            passes += chunk(W3D_CHUNK_TEXTURE_STAGE, stage, container=True)
        body += chunk(W3D_CHUNK_MATERIAL_PASS, passes, container=True)
        return chunk(W3D_CHUNK_MESH, bytes(body), container=True)


class Bone:
    def __init__(self, name, parent, translation=(0.0, 0.0, 0.0), rotation=(0.0, 0.0, 0.0, 1.0)):
        self.name = name
        self.parent = parent  # index or None
        self.translation = translation
        self.rotation = rotation  # quaternion x, y, z, w


class Model:
    """A hierarchical model: skeleton + meshes (+ optional boxes) + one HLOD."""

    def __init__(self, name, hierarchy_name=None):
        if len(name) >= NAME_LEN:
            raise ValueError("model name %r must be shorter than %d" % (name, NAME_LEN))
        self.name = name
        self.hierarchy_name = hierarchy_name or name
        self.bones = [Bone("ROOT", None)]
        self.meshes = []  # (mesh, bone index)

    def add_bone(self, name, parent=0, translation=(0, 0, 0), rotation=(0, 0, 0, 1)):
        if isinstance(parent, str):
            parent = self.bone_index(parent)
        self.bones.append(Bone(name, parent, translation, rotation))
        return len(self.bones) - 1

    def bone_index(self, name):
        for i, b in enumerate(self.bones):
            if b.name == name:
                return i
        raise KeyError(name)

    def add_mesh(self, mesh, bone=0):
        if isinstance(bone, str):
            bone = self.bone_index(bone)
        self.meshes.append((mesh, bone))
        return mesh

    def hierarchy_chunk(self):
        header = struct.pack("<I", W3D_HTREE_VERSION) + fixed_string(self.hierarchy_name, NAME_LEN)
        header += struct.pack("<I3f", len(self.bones), 0.0, 0.0, 0.0)
        pivots = bytearray()
        for b in self.bones:
            pivots += fixed_string(b.name, NAME_LEN)
            pivots += struct.pack("<I", NO_PARENT if b.parent is None else b.parent)
            pivots += struct.pack("<3f", *b.translation)
            pivots += struct.pack("<3f", 0.0, 0.0, 0.0)  # euler angles (unused by the loader)
            pivots += struct.pack("<4f", *b.rotation)
        body = chunk(W3D_CHUNK_HIERARCHY_HEADER, header) + chunk(W3D_CHUNK_PIVOTS, bytes(pivots))
        return chunk(W3D_CHUNK_HIERARCHY, body, container=True)

    def hlod_chunk(self):
        header = struct.pack("<II", W3D_HLOD_VERSION, 1)
        header += fixed_string(self.name, NAME_LEN) + fixed_string(self.hierarchy_name, NAME_LEN)
        sub = bytearray()
        sub += chunk(W3D_CHUNK_HLOD_SUB_OBJECT_ARRAY_HEADER, struct.pack("<If", len(self.meshes), 3.4028235e38))
        for mesh, bone in self.meshes:
            sub += chunk(W3D_CHUNK_HLOD_SUB_OBJECT,
                         struct.pack("<I", bone) + fixed_string("%s.%s" % (self.name, mesh.name), NAME_LEN * 2))
        body = chunk(W3D_CHUNK_HLOD_HEADER, header) + chunk(W3D_CHUNK_HLOD_LOD_ARRAY, bytes(sub), container=True)
        return chunk(W3D_CHUNK_HLOD, body, container=True)

    def to_bytes(self):
        """The complete ``.w3d`` file: hierarchy, meshes, HLOD."""
        if not self.meshes:
            raise ValueError("model %s has no meshes" % self.name)
        out = bytearray(self.hierarchy_chunk())
        for mesh, _bone in self.meshes:
            out += mesh.to_chunk(self.name)
        out += self.hlod_chunk()
        return bytes(out)


class Animation:
    """Per-bone translation/rotation channels, one value per frame.

    ``frames`` is the number of frames, ``fps`` the frame rate. Use
    :meth:`set_translation` / :meth:`set_rotation` with a callable of the frame index.
    """

    def __init__(self, name, hierarchy_name, frames, fps=30):
        self.name = name
        self.hierarchy_name = hierarchy_name
        self.frames = frames
        self.fps = fps
        self.channels = []  # (pivot, flag, vector_len, data)

    def set_translation(self, pivot, axis, func):
        """Animate one translation axis (0=X, 1=Y, 2=Z) with ``func(frame) -> offset``."""
        self.channels.append((pivot, axis, 1, [func(f) for f in range(self.frames)]))

    def set_rotation(self, pivot, func):
        """Animate the rotation with ``func(frame) -> (x, y, z, w)``."""
        data = []
        for f in range(self.frames):
            data.extend(func(f))
        self.channels.append((pivot, ANIM_CHANNEL_Q, 4, data))

    def to_bytes(self):
        header = struct.pack("<I", W3D_HANIM_VERSION) + fixed_string(self.name, NAME_LEN)
        header += fixed_string(self.hierarchy_name, NAME_LEN) + struct.pack("<II", self.frames, self.fps)
        out = bytearray(chunk(W3D_CHUNK_ANIMATION_HEADER, header))
        for pivot, flag, vlen, data in self.channels:
            payload = struct.pack("<HHHHHH", 0, self.frames - 1, vlen, flag, pivot, 0)
            payload += struct.pack("<%df" % len(data), *data)
            out += chunk(W3D_CHUNK_ANIMATION_CHANNEL, payload)
        return chunk(W3D_CHUNK_ANIMATION, bytes(out), container=True)


def quat_axis_angle(axis, angle):
    """Quaternion (x, y, z, w) for a rotation of ``angle`` radians about ``axis``."""
    ax = _norm(axis)
    s = math.sin(angle / 2.0)
    return (ax[0] * s, ax[1] * s, ax[2] * s, math.cos(angle / 2.0))


# ---- reader / validator -----------------------------------------------------
def iter_chunks(data, pos=0, end=None):
    """Yield ``(type, payload_offset, size, is_container)`` for the chunks in [pos, end)."""
    end = len(data) if end is None else end
    while pos + 8 <= end:
        ctype, size = struct.unpack_from("<II", data, pos)
        container = bool(size & SUB_CHUNK_FLAG)
        size &= ~SUB_CHUNK_FLAG & 0xFFFFFFFF
        if pos + 8 + size > end:
            raise ValueError("chunk 0x%x at %d overruns its parent" % (ctype, pos))
        yield ctype, pos + 8, size, container
        pos += 8 + size
    if pos != end:
        raise ValueError("trailing bytes after last chunk")


def parse_w3d(data):
    """Parse and validate a W3D file produced by this module.

    Returns a dict summarising what was found (meshes, hierarchy, hlod, animations).
    Raises ``ValueError`` on any structural inconsistency the engine's loader would
    reject (counts that do not match, chunk overruns, bad indices).
    """
    info = {"meshes": [], "hierarchy": None, "hlod": None, "animations": []}
    for ctype, off, size, _ in iter_chunks(data):
        if ctype == W3D_CHUNK_MESH:
            info["meshes"].append(_parse_mesh(data, off, size))
        elif ctype == W3D_CHUNK_HIERARCHY:
            info["hierarchy"] = _parse_hierarchy(data, off, size)
        elif ctype == W3D_CHUNK_HLOD:
            info["hlod"] = _parse_hlod(data, off, size)
        elif ctype == W3D_CHUNK_ANIMATION:
            info["animations"].append(_parse_anim(data, off, size))
        else:
            raise ValueError("unexpected top level chunk 0x%x" % ctype)
    if info["hlod"]:
        names = {m["full_name"] for m in info["meshes"]}
        for sub in info["hlod"]["objects"]:
            if sub["name"] not in names:
                raise ValueError("HLOD references missing mesh %s" % sub["name"])
            if info["hierarchy"] and sub["bone"] >= len(info["hierarchy"]["bones"]):
                raise ValueError("HLOD bone index out of range")
    return info


def _cstr(raw):
    return raw.split(b"\0", 1)[0].decode("ascii")


def _parse_mesh(data, off, size):
    sub = {c[0]: c for c in iter_chunks(data, off, off + size)}
    for required in (W3D_CHUNK_MESH_HEADER3, W3D_CHUNK_TRIANGLES, W3D_CHUNK_VERTICES, W3D_CHUNK_VERTEX_NORMALS):
        if required not in sub:
            raise ValueError("mesh is missing chunk 0x%x" % required)
    _, hoff, hsize, _ = sub[W3D_CHUNK_MESH_HEADER3]
    if hsize != 116:
        raise ValueError("mesh header size %d != 116" % hsize)
    version, attrs = struct.unpack_from("<II", data, hoff)
    name = _cstr(data[hoff + 8:hoff + 24])
    container = _cstr(data[hoff + 24:hoff + 40])
    ntris, nverts = struct.unpack_from("<II", data, hoff + 40)
    if sub[W3D_CHUNK_TRIANGLES][2] != ntris * 32:
        raise ValueError("triangle chunk size mismatch")
    if sub[W3D_CHUNK_VERTICES][2] != nverts * 12 or sub[W3D_CHUNK_VERTEX_NORMALS][2] != nverts * 12:
        raise ValueError("vertex chunk size mismatch")
    toff = sub[W3D_CHUNK_TRIANGLES][1]
    for i in range(ntris):
        idx = struct.unpack_from("<III", data, toff + i * 32)
        if max(idx) >= nverts:
            raise ValueError("triangle %d indexes past the vertex array" % i)
    textures = []
    if W3D_CHUNK_TEXTURES in sub:
        _, xoff, xsize, _ = sub[W3D_CHUNK_TEXTURES]
        for ctype, o, s, _ in iter_chunks(data, xoff, xoff + xsize):
            for c2, o2, s2, _ in iter_chunks(data, o, o + s):
                if c2 == W3D_CHUNK_TEXTURE_NAME:
                    textures.append(_cstr(data[o2:o2 + s2]))
    minfo = struct.unpack_from("<IIII", data, sub[W3D_CHUNK_MATERIAL_INFO][1])
    if minfo[3] != len(textures):
        raise ValueError("material info texture count mismatch")
    if _texcoords_ok(data, sub, nverts) is False:
        raise ValueError("texture coordinate count mismatch")
    return {"name": name, "container": container, "full_name": ("%s.%s" % (container, name)) if container else name,
            "triangles": ntris, "vertices": nverts, "textures": textures, "attributes": attrs}


def _texcoords_ok(data, sub, nverts):
    """True if the (optional) texture coordinate array matches the vertex count."""
    if W3D_CHUNK_MATERIAL_PASS not in sub:
        return True
    _, poff, psize, _ = sub[W3D_CHUNK_MATERIAL_PASS]
    for ctype, o, s, _ in iter_chunks(data, poff, poff + psize):
        if ctype == W3D_CHUNK_TEXTURE_STAGE:
            for c2, o2, s2, _ in iter_chunks(data, o, o + s):
                if c2 == W3D_CHUNK_STAGE_TEXCOORDS and s2 != nverts * 8:
                    return False
    return True


def _parse_hierarchy(data, off, size):
    sub = {c[0]: c for c in iter_chunks(data, off, off + size)}
    _, hoff, hsize, _ = sub[W3D_CHUNK_HIERARCHY_HEADER]
    name = _cstr(data[hoff + 4:hoff + 20])
    (count,) = struct.unpack_from("<I", data, hoff + 20)
    _, poff, psize, _ = sub[W3D_CHUNK_PIVOTS]
    if psize != count * 60:
        raise ValueError("pivot chunk size mismatch")
    bones = []
    for i in range(count):
        base = poff + i * 60
        bname = _cstr(data[base:base + 16])
        (parent,) = struct.unpack_from("<I", data, base + 16)
        if i == 0 and parent != NO_PARENT:
            raise ValueError("first bone must be the root")
        if i > 0 and parent >= i:
            raise ValueError("bone %s has a forward parent reference" % bname)
        bones.append((bname, parent))
    return {"name": name, "bones": bones}


def _parse_hlod(data, off, size):
    sub = list(iter_chunks(data, off, off + size))
    _, hoff, hsize, _ = sub[0]
    version, lod_count = struct.unpack_from("<II", data, hoff)
    name = _cstr(data[hoff + 8:hoff + 24])
    hier = _cstr(data[hoff + 24:hoff + 40])
    objects = []
    for ctype, o, s, _ in sub[1:]:
        if ctype != W3D_CHUNK_HLOD_LOD_ARRAY:
            continue
        inner = list(iter_chunks(data, o, o + s))
        (count, _screen) = struct.unpack_from("<If", data, inner[0][1])
        if count != len(inner) - 1:
            raise ValueError("HLOD sub object count mismatch")
        for c2, o2, s2, _ in inner[1:]:
            (bone,) = struct.unpack_from("<I", data, o2)
            objects.append({"bone": bone, "name": _cstr(data[o2 + 4:o2 + 36])})
    if lod_count != sum(1 for c in sub if c[0] == W3D_CHUNK_HLOD_LOD_ARRAY):
        raise ValueError("HLOD lod count mismatch")
    return {"name": name, "hierarchy": hier, "objects": objects}


def _parse_anim(data, off, size):
    sub = list(iter_chunks(data, off, off + size))
    _, hoff, hsize, _ = sub[0]
    name = _cstr(data[hoff + 4:hoff + 20])
    hier = _cstr(data[hoff + 20:hoff + 36])
    frames, fps = struct.unpack_from("<II", data, hoff + 36)
    channels = 0
    for ctype, o, s, _ in sub[1:]:
        if ctype == W3D_CHUNK_ANIMATION_CHANNEL:
            first, last, vlen, flag, pivot, _pad = struct.unpack_from("<HHHHHH", data, o)
            if s != 12 + (last - first + 1) * vlen * 4:
                raise ValueError("animation channel size mismatch")
            channels += 1
    return {"name": name, "hierarchy": hier, "frames": frames, "fps": fps, "channels": channels}
