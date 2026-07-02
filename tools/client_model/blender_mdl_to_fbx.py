#!/usr/bin/env python3
"""blender_mdl_to_fbx -- build a rigged, textured FBX from mdl_extract output.

Run headless under Blender:
  blender --background --python blender_mdl_to_fbx.py -- <srcDir> <out.fbx>

<srcDir> is the mdl_extract output dir (model.obj + model.bones.json + tex/*.png).
Produces an FBX that carries, straight from the GoldSrc .mdl:
  - the mesh (bind pose) + UVs
  - one material per source texture, image embedded
  - the HL "Bip01" skeleton (world positions from model.bones.json["world"])
  - the ORIGINAL rigid 1-bone-per-vertex weights (vertex groups, weight 1.0)
  - an Armature modifier binding mesh->skeleton

So opening the FBX in Blender gives the model with its original mdl rig + weights
intact -- ready to retarget onto the VOTV anthro rig (dr_kel.psk).

Dev/RE tool (RULE 3); output is derived content -> keep gitignored.
"""
import bpy
import json
import math
import os
import sys


def parse_obj(path):
    verts, uvs, faces, face_uv, face_mat, mats, matidx = [], [], [], [], [], [], {}
    cur = 0
    for line in open(path):
        if line.startswith("v "):
            x, y, z = map(float, line.split()[1:4]); verts.append((x, y, z))
        elif line.startswith("vt "):
            u, v = map(float, line.split()[1:3]); uvs.append((u, v))
        elif line.startswith("usemtl"):
            m = line.split()[1]
            if m not in matidx:
                matidx[m] = len(mats); mats.append(m)
            cur = matidx[m]
        elif line.startswith("f "):
            c = []
            for tok in line.split()[1:]:
                a = tok.split("/")
                c.append((int(a[0]) - 1, int(a[1]) - 1 if len(a) > 1 and a[1] else -1))
            faces.append((c[0][0], c[1][0], c[2][0]))
            face_uv.append((c[0][1], c[1][1], c[2][1]))
            face_mat.append(cur)
    return verts, uvs, faces, face_uv, face_mat, mats


def main():
    argv = sys.argv[sys.argv.index("--") + 1:]
    srcdir, outfbx = argv[0], argv[1]

    verts, uvs, faces, face_uv, face_mat, mats = parse_obj(os.path.join(srcdir, "model.obj"))
    meta = json.load(open(os.path.join(srcdir, "model.bones.json")))
    bones = meta["bones"]; vert_bone = meta["vert_bone"]; world = meta["world"]

    bpy.ops.wm.read_factory_settings(use_empty=True)

    # ---- mesh ----
    me = bpy.data.meshes.new("scientist")
    me.from_pydata(verts, [], faces)
    me.update()
    obj = bpy.data.objects.new("scientist", me)
    bpy.context.collection.objects.link(obj)

    # UVs (per face-corner) -- polygon order == input face order
    uvl = me.uv_layers.new(name="UVMap")
    for i, poly in enumerate(me.polygons):
        fu = face_uv[i]
        for k, li in enumerate(poly.loop_indices):
            vt = fu[k]
            uvl.data[li].uv = uvs[vt] if 0 <= vt < len(uvs) else (0.0, 0.0)
        poly.material_index = face_mat[i]

    # materials + embedded image textures (one per source .mdl texture)
    for m in mats:
        mat = bpy.data.materials.new(m); mat.use_nodes = True
        png = os.path.join(srcdir, "tex", m + ".png")
        if os.path.exists(png):
            img = bpy.data.images.load(png)
            nt = mat.node_tree
            bsdf = next((n for n in nt.nodes if n.type == "BSDF_PRINCIPLED"), None)
            tex = nt.nodes.new("ShaderNodeTexImage"); tex.image = img
            if bsdf:
                nt.links.new(bsdf.inputs["Base Color"], tex.outputs["Color"])
        me.materials.append(mat)
    me.validate(verbose=False)

    # ---- armature (HL Bip01 skeleton, world positions) ----
    heads = [(w[3], w[7], w[11]) for w in world]
    xs = [h[0] for h in heads]; ys = [h[1] for h in heads]; zs = [h[2] for h in heads]
    diag = math.sqrt((max(xs) - min(xs)) ** 2 + (max(ys) - min(ys)) ** 2 + (max(zs) - min(zs)) ** 2)
    stub = max(diag * 0.03, 0.5)

    children = {}
    for i, bn in enumerate(bones):
        p = bn["parent"]
        if p >= 0:
            children.setdefault(p, []).append(i)

    arm = bpy.data.armatures.new("HL_Armature")
    armobj = bpy.data.objects.new("Armature", arm)
    bpy.context.collection.objects.link(armobj)
    bpy.context.view_layer.objects.active = armobj
    bpy.ops.object.mode_set(mode="EDIT")
    ebs = []
    for i, bn in enumerate(bones):
        eb = arm.edit_bones.new(bn["name"])
        h = heads[i]
        ch = children.get(i, [])
        if ch:
            chh = [heads[c] for c in ch]
            t = tuple(sum(a) / len(chh) for a in zip(*chh))
            if (t[0] - h[0]) ** 2 + (t[1] - h[1]) ** 2 + (t[2] - h[2]) ** 2 < 1e-6:
                t = (h[0], h[1], h[2] + stub)
        else:
            p = bn["parent"]
            if p >= 0:
                ph = heads[p]
                d = (h[0] - ph[0], h[1] - ph[1], h[2] - ph[2])
                L = math.sqrt(d[0] ** 2 + d[1] ** 2 + d[2] ** 2) or 1.0
                t = (h[0] + d[0] / L * stub, h[1] + d[1] / L * stub, h[2] + d[2] / L * stub)
            else:
                t = (h[0], h[1], h[2] + stub)
        eb.head = h; eb.tail = t
        ebs.append(eb)
    for i, bn in enumerate(bones):
        if bn["parent"] >= 0:
            ebs[i].parent = ebs[bn["parent"]]
    bpy.ops.object.mode_set(mode="OBJECT")

    # ---- weights (original rigid 1-bone-per-vertex) + bind ----
    vg = {bn["name"]: obj.vertex_groups.new(name=bn["name"]) for bn in bones}
    for vi, bidx in enumerate(vert_bone):
        vg[bones[bidx]["name"]].add([vi], 1.0, "REPLACE")
    mod = obj.modifiers.new("Armature", "ARMATURE"); mod.object = armobj
    obj.parent = armobj

    # ---- export ----
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True); armobj.select_set(True)
    bpy.context.view_layer.objects.active = armobj
    bpy.ops.export_scene.fbx(
        filepath=outfbx,
        use_selection=True,
        object_types={"ARMATURE", "MESH"},
        add_leaf_bones=False,
        bake_anim=False,
        path_mode="COPY",
        embed_textures=True,
        mesh_smooth_type="OFF",
        use_custom_props=False,
    )
    print(f"[blender_mdl_to_fbx] wrote {outfbx}  verts={len(verts)} tris={len(faces)} "
          f"mats={len(mats)} bones={len(bones)}")


if __name__ == "__main__":
    main()
