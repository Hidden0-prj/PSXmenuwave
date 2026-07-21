import sys

# The GTE takes plain integer model-space coordinates for vertices (it is the
# 3x3 rotation MATRIX that is stored in 4096-scaled fixed point, not the
# vertices themselves). We just need to scale the model up from Blender's
# "meters-ish" units into a reasonable number of PS1 world units so it isn't
# a fraction of a unit wide. 100 gives a model roughly on the same order of
# size as the SDK's own spinning cube example.
VERTEX_SCALE = 100

# Scale UVs to 0-127 (for a 128x128 texture, PS1 UV coords are always 0-255
# texels but our texture is only 128px wide/tall so 0-127 is the valid range)
UV_SCALE = 127


def convert_textured_obj(obj_path, h_path):
    vertices = []
    uvs = []
    faces = []  # each entry: (v_idx[3], vt_idx[3]) - already triangulated

    with open(obj_path, 'r') as f:
        for line in f:
            tokens = line.strip().split()
            if not tokens:
                continue

            if tokens[0] == 'v':
                vertices.append((
                    int(float(tokens[1]) * VERTEX_SCALE),
                    int(float(tokens[2]) * VERTEX_SCALE),
                    int(float(tokens[3]) * VERTEX_SCALE),
                ))
            elif tokens[0] == 'vt':
                # Blender's V axis is inverted relative to the PS1 VRAM axis
                uvs.append((
                    int(float(tokens[1]) * UV_SCALE),
                    int((1.0 - float(tokens[2])) * UV_SCALE),
                ))
            elif tokens[0] == 'f':
                face_v = []
                face_vt = []
                # A face can have 3, 4, or more vertices (tris/quads/n-gons).
                # Parse ALL of them, not just the first 3 - dropping the 4th
                # vertex of a quad silently deletes half of every quad face!
                for token in tokens[1:]:
                    parts = token.split('/')
                    face_v.append(int(parts[0]) - 1)
                    if len(parts) > 1 and parts[1]:
                        face_vt.append(int(parts[1]) - 1)
                    else:
                        face_vt.append(-1)

                # Fan-triangulate n-gons (this also handles plain triangles,
                # where the loop below runs exactly once).
                for i in range(1, len(face_v) - 1):
                    faces.append((
                        (face_v[0], face_v[i], face_v[i + 1]),
                        (face_vt[0], face_vt[i], face_vt[i + 1]),
                    ))

    with open(h_path, 'w') as out:
        out.write("#ifndef MODEL_DATA_H\n#define MODEL_DATA_H\n\n#include <stdint.h>\n\n")

        # Deliberately NOT reusing the PS1 SDK's GTEVector16 name/type here so
        # this header has no dependency on SDK include paths; main.c casts to
        # GTEVector16* when passing these to the GTE (identical memory layout:
        # 3x int16_t + int16_t padding, aligned to 4 bytes).
        out.write("typedef struct __attribute__((aligned(4))) {\n    int16_t x, y, z, pad;\n} SVECTOR;\n\n")

        # NOTE: field names below must all be unique within the struct -
        # naming the UV fields u0/v0/u1/v1/u2/v2 collided with the v0/v1/v2
        # vertex-index fields above and would not compile.
        out.write(
            "typedef struct {\n"
            "    uint16_t v0, v1, v2;\n"
            "    uint8_t  tu0, tv0, tu1, tv1, tu2, tv2;\n"
            "} MESH_FACE;\n\n"
        )

        out.write(f"const int vertex_count = {len(vertices)};\n")
        out.write("const SVECTOR model_vertices[] = {\n")
        for v in vertices:
            out.write(f"    {{ {v[0]}, {v[1]}, {v[2]}, 0 }},\n")
        out.write("};\n\n")

        out.write(f"const int face_count = {len(faces)};\n")
        out.write("const MESH_FACE model_faces[] = {\n")
        for v_idx, vt_idx in faces:
            u0, v0 = uvs[vt_idx[0]] if 0 <= vt_idx[0] < len(uvs) else (0, 0)
            u1, v1 = uvs[vt_idx[1]] if 0 <= vt_idx[1] < len(uvs) else (0, 0)
            u2, v2 = uvs[vt_idx[2]] if 0 <= vt_idx[2] < len(uvs) else (0, 0)
            out.write(
                f"    {{ {v_idx[0]}, {v_idx[1]}, {v_idx[2]}, "
                f"{u0}, {v0}, {u1}, {v1}, {u2}, {v2} }},\n"
            )
        out.write("};\n\n#endif // MODEL_DATA_H\n")

    print(f"Generated {h_path} with {len(vertices)} vertices and {len(faces)} triangles (from OBJ tris/quads/n-gons).")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python3 obj_to_c.py <input.obj> <output.h>")
    else:
        convert_textured_obj(sys.argv[1], sys.argv[2])
