"""List the alternate native guard signatures for the game's IWRAM overlay."""
import argparse
from pathlib import Path
import re


BLOCK = re.compile(
    r"uint32_t b_([0-9a-f]{8})_a\(const RtApi\* a, void\* m\) \{\n"
    r"(?:[^\n]+\n){2}    if \(!a->guard\(m, 0x[0-9a-f]{8}u, (\d+)u, (0x[0-9a-f]+)ull\)"
)


def collect(folder):
    output = {}
    for unit in sorted(folder.glob("vrally3-us.*.c")):
        for address, count, digest in BLOCK.findall(unit.read_text()):
            addr = int(address, 16)
            if 0x03007700 <= addr < 0x03007a00:
                output[addr] = (int(count), digest)
    return output


def main():
    args = argparse.ArgumentParser()
    args.add_argument("primary", type=Path)
    args.add_argument("alternate", type=Path)
    args.add_argument("header", type=Path)
    a = args.parse_args()
    primary, alternate = collect(a.primary), collect(a.alternate)
    entries = []
    for addr in sorted(primary.keys() & alternate.keys()):
        if primary[addr] != alternate[addr]:
            p_len, p_hash = primary[addr]
            a_len, a_hash = alternate[addr]
            entries.append(f"    {{0x{addr:08x}u, {p_len}u, {a_len}u, {p_hash}ull, {a_hash}ull}},")
    if not entries:
        raise ValueError("Nenhuma variante sobreposta encontrada")
    a.header.write_text("static const Overlay overlay_specs[] = {\n" + "\n".join(entries) + "\n};\n", encoding="ascii")
    print(f"{len(entries)} variantes IWRAM auditadas")


if __name__ == "__main__":
    main()

