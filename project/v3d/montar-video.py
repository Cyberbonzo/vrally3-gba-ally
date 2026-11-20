"""Build the V3D video module against the locally generated translation."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from instrumentar import instrument, wide_projection


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("generated", type=Path, help="Primary C translation after ARM lowering")
    parser.add_argument("native", type=Path, help="Directory of the installed CPU-only translation")
    parser.add_argument("output", type=Path, help="Destination for the three HD libraries")
    args = parser.parse_args()

    here = Path(__file__).resolve().parent
    units = sorted(args.generated.resolve().glob("vrally3-us.*.c"))
    marker = "uint32_t b_030033fc_a(const RtApi* a, void* m) {"
    matches = [p for p in units if marker in p.read_text()]
    if len(matches) != 1:
        raise SystemExit("Expected exactly one primary V3D dispatcher")
    primary = matches[0]
    args.output.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="v3d-build-", dir=args.output) as work:
        temp = Path(work).resolve()
        wide = temp / primary.name
        wide.write_text(instrument(wide_projection(primary.read_text(), live=True)))
        for source, target in (
            ("video-ao-vivo.c", "video.o"),
            ("render-v3d.c", "render.o"),
        ):
            subprocess.run(
                ["cc", "-O3", "-Wall", "-Wextra", "-Werror", "-fPIC", "-c",
                 here / source, "-o", temp / target],
                check=True,
            )
        subprocess.run(
            ["cc", "-O0", "-shared", "-fPIC", "-I", here, "-o", temp / "original.so",
             *[wide if p == primary else p for p in units],
             temp / "video.o", temp / "render.o", "-lm"],
            check=True,
        )
        for name in ("additional.so", "vrally3-us.so"):
            shutil.copy2(args.native / name, temp / name)
        for name in ("original.so", "additional.so", "vrally3-us.so"):
            os.replace(temp / name, args.output / name)


if __name__ == "__main__":
    main()

