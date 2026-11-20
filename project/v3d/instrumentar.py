import argparse
from pathlib import Path
import re
import subprocess


HOOKS = ("030033fc", "0300346c")


def instrument(source):
    for address in HOOKS:
        signature = re.compile(
            rf"(uint32_t b_{address}_a\(const RtApi\* a, void\* m\) \{{.*?"
            rf"if \(!a->guard\(m, 0x{address}u, [^\n]+\n)",
            re.DOTALL,
        )
        source, count = signature.subn(r"\1" + f"    v3d_trace(a, m, 0x{address}u);\n", source, count=1)
        if count != 1:
            raise ValueError(f"Bloco esperado ausente em {address}")
    return "extern void v3d_trace(const void *, void *, unsigned int);\n" + source


def wide_projection(source, live=False):
    # The GBA viewport remains 240 units wide, so horizontal projection
    # contracts by 27/32 before the host maps it to 1920x1080. The host
    # pixel focal lengths then remain equal on both axes. Move the two
    # horizontal clipping planes out to cover that larger field of view.
    header = re.compile(r"^uint32_t b_([0-9a-f]{8})_a\(const RtApi\* a, void\* m\) \{", re.M)
    chunks = list(header.finditer(source))
    counts = [0, 0, 0]
    sprite_edges = 0
    parts = [source[:chunks[0].start()]]
    for index, match in enumerate(chunks):
        addr = int(match.group(1), 16)
        end = chunks[index + 1].start() if index + 1 < len(chunks) else len(source)
        block = source[match.start():end]
        clipping = any(lo <= addr <= hi for lo, hi in (
            (0x03002138, 0x03002148), (0x03002B78, 0x03002BB4), (0x03002F5C, 0x03002F98),
            (0x0300525C, 0x0300526C), (0x03005B20, 0x03005B5C), (0x03005F00, 0x03005F3C),
        ))
        if clipping:
            for k, old, new in (
                (0, "uint32_t r = 0x0000005du;", "uint32_t r = 0x0000004eu;"),
                (1, "uint32_t r = (~0x0000005cu);", "uint32_t r = (~0x0000004du);"),
            ):
                if live:
                    original_value = old.removeprefix("uint32_t r = ").removesuffix(";")
                    wide_value = new.removeprefix("uint32_t r = ").removesuffix(";")
                    new = f"uint32_t r = v3d_race_projection ? {wide_value} : {original_value};"
                counts[k] += block.count(old)
                block = block.replace(old, new)
        if 0x03003114 <= addr <= 0x03003178 or 0x030060B8 <= addr <= 0x03006108:
            old = "op_adds(c, 0x00000780u, ~c[7], 1u)"
            new = "op_adds(c, 0x00000780u, ~(uint32_t)(((int64_t)(int32_t)c[7] * 27) / 32), 1u)"
            if live:
                new = f"(v3d_race_projection ? {new} : {old})"
            counts[2] += block.count(old)
            block = block.replace(old, new)
        # Billboard edges must be projected before the guest clips the
        # rectangle and its UVs to [0, 240]. Both edges use the same scale.
        if live and addr == 0x03004E6C:
            for register in (6, 10):
                old = f"0x00000078u + ~c[{register}] + 1u"
                scaled = f"(uint32_t)(((int64_t)(int32_t)c[{register}] * 27) / 32)"
                new = f"0x78u - (v3d_race_projection ? {scaled} : c[{register}])"
                sprite_edges += block.count(old)
                block = block.replace(old, new)
        parts.append(block)
    if live and sprite_edges != 2:
        raise ValueError(f"Expected two billboard edges, found {sprite_edges}")
    if min(counts) == 0:
        raise ValueError(f"Rotinas de projeÃƒÂ§ÃƒÂ£o nÃƒÂ£o localizadas: {counts}")
    print("ProjeÃƒÂ§ÃƒÂ£o e recorte horizontal", counts)
    return ("extern int v3d_race_projection;\n" if live else "") + "".join(parts)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("generated", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--wide", action="store_true")
    args = parser.parse_args()
    out = args.output.resolve()
    out.parent.mkdir(parents=True, exist_ok=True)
    units = sorted(args.generated.glob("vrally3-us.*.c"))
    matches = [p for p in units if "uint32_t b_030033fc_a(const RtApi* a, void* m) {" in p.read_text()]
    if len(matches) != 1:
        raise ValueError("Rotina V3D esperada ausente")
    patched = out.parent / "v3d-instrumentado.c"
    text = matches[0].read_text()
    if args.wide:
        text = wide_projection(text)
    patched.write_text(instrument(text))
    sources = [patched if p == matches[0] else p for p in units]
    sources.append(Path(__file__).with_name("trace.c"))
    subprocess.run(["cc", "-shared", "-fPIC", "-O0", "-o", out, *sources], check=True)


if __name__ == "__main__":
    main()

