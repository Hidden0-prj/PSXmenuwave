import sys

try:
    from PIL import Image
except ImportError:
    print("This script requires Pillow. Install it with: pip3 install pillow")
    sys.exit(1)


def convert_texture(png_path, h_path, prefix="TEXTURE"):
    img = Image.open(png_path).convert("RGBA")
    width, height = img.size

    if width > 256 or height > 256:
        raise SystemExit(
            f"Texture is {width}x{height}, but the PS1 GPU does not support "
            "textures larger than 256x256 in a single texture page."
        )

    pixels = list(img.getdata())

    out_words = []
    for (r, g, b, a) in pixels:
        if a < 128:
            # Real transparency (e.g. a font atlas's background). Pixel value
            # 0x0000 is the PS1 GPU's hardware code for "fully transparent
            # when texture mapping" - this is the one case we WANT that.
            out_words.append(0)
            continue

        # PS1 native 16bpp format: 1 STP bit, 5 bits each for B, G, R.
        r5 = r >> 3
        g5 = g >> 3
        b5 = b >> 3
        stp = 0
        value = (stp << 15) | (b5 << 10) | (g5 << 5) | r5

        # Pixel value 0x0000 is a hardware special case: the GPU always
        # treats it as fully transparent when texture mapping, regardless of
        # blending mode. Nudge true OPAQUE black pixels so they don't
        # accidentally vanish (this only applies to opaque pixels - real
        # transparency, handled above, is left alone).
        if value == 0:
            value = 1

        out_words.append(value)

    guard = f"{prefix}_DATA_H"
    array_name = f"{prefix.lower()}TextureData"

    with open(h_path, 'w') as out:
        out.write(f"#ifndef {guard}\n#define {guard}\n\n#include <stdint.h>\n\n")
        out.write(f"#define {prefix}_WIDTH  {width}\n")
        out.write(f"#define {prefix}_HEIGHT {height}\n\n")
        out.write("// Raw PS1 16bpp (1 STP + 5B + 5G + 5R) texture data, row-major.\n")
        out.write(f"static const uint16_t {array_name}[] __attribute__((aligned(4))) = {{\n")
        for i in range(0, len(out_words), 16):
            row = out_words[i:i + 16]
            out.write("    " + ", ".join(f"0x{w:04x}" for w in row) + ",\n")
        out.write("};\n\n#endif // " + guard + "\n")

    print(f"Generated {h_path} from {png_path} ({width}x{height}, {len(out_words)} texels).")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python3 png_to_c.py <input.png> <output.h> [PREFIX]")
    else:
        prefix_arg = sys.argv[3] if len(sys.argv) > 3 else "TEXTURE"
        convert_texture(sys.argv[1], sys.argv[2], prefix_arg)
