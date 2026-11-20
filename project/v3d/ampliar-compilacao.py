"""Lower selected ARM7TDMI operations that the stock generator delegates."""
import argparse
from collections import Counter
from pathlib import Path
import re
import struct


CALL = re.compile(
    r"(?m)^(    c\[15\] = 0x([0-9a-f]{8})u;\n    (?:pc = |return ))a->interp_one\(m\)"
)


def reg(n, pc):
    return f"c[{n}]" if n < 15 else f"0x{pc:08x}u"


def immediate_shift(n, kind, amount, pc):
    value = reg(n, pc)
    if kind == 0:
        return f"({value} << {amount}u)"
    if kind == 1:
        return f"({value} >> {amount}u)" if amount else "0u"
    if kind == 2:
        return f"(uint32_t)((int32_t){value} >> {amount or 31}u)"
    if not amount:
        return f"((c[16] & 0x20000000u) << 2 | ({value} >> 1))"
    return f"vr_ror({value}, {amount}u)"


def arm_statement(code, pc):
    cond = code >> 28
    if code & 0x0ffffff0 == 0x012fff10 and cond < 15:
        r = code & 15
        if r == 15:
            return None
        cost = 3 if pc >> 24 == 3 else 6
        return "arm-bx", f"vr_bx(a,m,c,c[{r}],{cond}u,0x{pc + 4:08x}u,{cost}u)"
    if cond != 14:
        return None
    if code & 0x0c500000 == 0x04100000 and (code >> 12 & 15) == 15 and code & 0x01000000 and not code & 0x00200000:
        # ARM LDR pc pre-indexed, no writeback. Loads do not change CPU mode.
        base = reg(code >> 16 & 15, pc + 8)
        if code & 0x02000000:
            if code & 16:
                return None
            index = immediate_shift(code & 15, code >> 5 & 3, code >> 7 & 31, pc + 8)
        else:
            index = f"{code & 4095}u"
        sign = "+" if code & 0x00800000 else "-"
        return "ldr-pc", f"vr_load_pc(a,m,c,({base}) {sign} ({index}),{3 if pc >> 24 == 3 else 6}u)"
    if code & 0x0e000000 or code & 0x00100000:
        return None
    op = code >> 21 & 15
    dst = code >> 12 & 15
    first = code >> 16 & 15
    second = code & 15
    if code & 16:
        if code & 128 or op not in (0, 1, 2, 3, 4, 12, 13, 14, 15) or dst == 15:
            return None
        shift_reg = code >> 8 & 15
        kind = code >> 5 & 3
        rhs = f"vr_shift({reg(second, pc + 12)},{reg(shift_reg, pc + 8)} & 255u,{kind}u,0,0)"
        lhs = reg(first, pc + 12)
        expr = {0: f"{lhs} & ({rhs})", 1: f"{lhs} ^ ({rhs})", 2: f"{lhs} - ({rhs})",
                3: f"({rhs}) - {lhs}", 4: f"{lhs} + ({rhs})", 12: f"{lhs} | ({rhs})",
                13: rhs, 14: f"{lhs} & ~({rhs})", 15: f"~({rhs})"}[op]
        return "arm-alu-shift", f"vr_write(a,m,c,{dst}u,{expr},0x{pc + 4:08x}u,{1 if pc >> 24 == 3 else 4}u)"

    if dst != 15 or op not in (2, 3, 4, 13):
        return None
    tail = immediate_shift(second, code >> 5 & 3, code >> 7 & 31, pc + 8)
    head = reg(first, pc + 8)
    expr = {2: f"{head} - ({tail})", 3: f"({tail}) - {head}",
            4: f"{head} + ({tail})", 13: tail}[op]
    return "arm-alu-pc", f"vr_write(a,m,c,15u,{expr},0u,{1 if pc >> 24 == 3 else 4}u)"


def thumb_statement(code, pc):
    if code & 0xff87 == 0x4687:
        src = code >> 3 & 15
        if src != 15:
            return "thumb-mov-pc", f"vr_thumb_pc(a,m,c,c[{src}],{1 if pc >> 24 == 3 else 2}u)"
    if code & 0xfc00 != 0x4000:
        return None
    kind = {2: 0, 3: 1, 4: 2, 7: 3}.get(code >> 6 & 15)
    if kind is None:
        return None
    dst, src = code & 7, code >> 3 & 7
    tick = 1 if pc >> 24 == 3 else 2
    return "thumb-alu-shift", f"vr_thumb_shift(a,m,c,{dst}u,c[{src}] & 255u,{kind}u,0x{pc + 2:08x}u,{tick}u)"


def specialize(text, rom, iwram, counts):
    def replace(match):
        pc = int(match.group(2), 16)
        if 0x03000000 <= pc < 0x03007800:
            offset = pc - 0x03000000
            code = struct.unpack_from("<I", iwram, offset)[0]
            translated = arm_statement(code, pc)
            condition = f"a->read32(m, 0x{pc:08x}u) == 0x{code:08x}u"
        elif 0x08000000 <= pc < 0x08000000 + len(rom) - 4:
            offset = pc - 0x08000000
            head = text.rfind("uint32_t b_", 0, match.start())
            if head < 0:
                return match.group()
            thumb = text[head:text.find("(", head)].endswith("_t")
            code = struct.unpack_from("<H" if thumb else "<I", rom, offset)[0]
            translated = thumb_statement(code, pc) if thumb else arm_statement(code, pc)
            condition = None
        else:
            return match.group()
        if translated is None:
            return match.group()
        name, expr = translated
        counts[name] += 1
        if condition is not None:
            expr = f"({condition} ? {expr} : a->interp_one(m))"
        return match.group(1) + expr

    insertion = "} RtApi;"
    if text.count(insertion) != 1:
        raise ValueError("ABI gerada pelo recompilador mudou")
    text = text.replace(insertion, insertion + '\n#include "arm-aot.h"', 1)
    return CALL.sub(replace, text)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("rom", type=Path)
    parser.add_argument("iwram", type=Path)
    args = parser.parse_args()
    args.destination.mkdir(parents=True, exist_ok=True)
    rom, ram = args.rom.read_bytes(), args.iwram.read_bytes()
    if len(ram) != 0x8000 or len(rom) != 0x400000:
        raise ValueError("ROM ou IWRAM com tamanho incorreto")
    counts = Counter()
    for source in sorted(args.source.glob("vrally3-us.*.c")):
        target = args.destination / source.name
        target.write_text(specialize(source.read_text(), rom, ram, counts), encoding="utf-8")
    if not counts:
        raise ValueError("Nenhuma emissão nativa adicional")
    print(dict(counts))


if __name__ == "__main__":
    main()

