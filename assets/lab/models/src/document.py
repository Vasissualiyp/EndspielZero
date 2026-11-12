# SPDX-License-Identifier: GPL-3.0-or-later
"""Refresh the asset inventory from independently validated final OBJ files."""
from validate import ROOT, SPECS, validate


def number(x):
    return f'{x:.9f}'.rstrip('0').rstrip('.') if x else '0'


def vector(p):
    return '('+', '.join(number(x) for x in p)+')'


def main():
    reports=[validate(ROOT/(name+'.obj')) for name in SPECS]
    lines=[
        '<!-- SPDX-License-Identifier: GPL-3.0-or-later -->',
        '# M24 lab models',
        '',
        'All 17 requested OBJ files are complete. Each was generated, independently validated,',
        'and visually inspected in three-quarter front and side PNG previews. Units are metres;',
        'Z is up. The default front is −Y, except the magazine (−X) and roller door (+Y).',
        '',
        '## Provenance and licence',
        '',
        'Every model is original procedural geometry authored for EndspielZero under',
        '**GPL-3.0-or-later**. This delivery continues the two local generator scripts left',
        'by the interrupted run; no finished OBJ existed when work resumed. No third-party',
        'models, downloads, textures, external scripts or executable assets were used.',
        'Each OBJ includes a `# source:` comment recording this original provenance.',
        'The CC0-only rule for web imports therefore did not need to be exercised.',
        '',
        '## Inventory',
        '',
        'Bounds below are exact exported, axis-aligned minima and maxima in **(X, Y, Z)**',
        'metres, not dimensions. The origin column describes where local (0, 0, 0) belongs.',
        'Vertex counts include deliberate duplicates at hard normal seams.',
        '',
        '| File | Triangles | Vertices | Bounds min → max (m) | Origin / orientation |',
        '|---|---:|---:|---|---|',
    ]
    for r in reports:
        lines.append(f"| [{r['file']}]({r['file']}) | {r['triangles']:,} | {r['vertices']:,} | `{vector(r['bounds_min'])}` → `{vector(r['bounds_max'])}` | {r['origin']} |")
    lines += [
        '',
        f"Total: **{sum(r['triangles'] for r in reports):,} triangles** across 17 files. Largest file: "
        f"**{max(r['triangles'] for r in reports):,} triangles**, below the 6,000 limit.",
        '',
        '## Regeneration',
        '',
        'From the repository root, run the commands below in order using Python 3; all',
        'scripts use only its standard library. `generate.py` constructs the named parts',
        'using the chamfered box, lathe, tube and swept-wire primitives in `mesh.py`.',
        '`validate.py` re-reads the exported ASCII OBJ data and writes machine-readable',
        'measurements; `preview.py` uses a CPU depth buffer and interpolated normal lighting',
        'to write two PNG views per file plus contact sheets. `document.py` refreshes this',
        'inventory from validated geometry. Every command writes only within',
        '`assets/lab/models/` or `out/m24-models/`.',
        '',
        '```bash',
        'PYTHONDONTWRITEBYTECODE=1 python3 assets/lab/models/src/generate.py',
        'PYTHONDONTWRITEBYTECODE=1 python3 assets/lab/models/src/validate.py --self-test > out/m24-models/validation.txt',
        'PYTHONDONTWRITEBYTECODE=1 python3 assets/lab/models/src/preview.py',
        'PYTHONDONTWRITEBYTECODE=1 python3 assets/lab/models/src/document.py',
        '```',
        '',
        'The generator is deterministic. `out/m24-models/reproducibility.txt` records the',
        'byte-for-byte regeneration check and `out/m24-models/sha256.json` records hashes.',
        '',
        '## Validation and construction details',
        '',
        '- Only `v`, `vn`, triangular `f`, `o`, `g` and comments appear. No materials or UVs.',
        '- The independent validator checks finite data, indices, budgets, ±5% size and',
        '  origin envelopes, degenerate/duplicate faces, unit normals and winding agreement.',
        '- Every positional edge is used twice with opposite directions after welding',
        '  hard-normal vertex duplicates within each component. Each connected shell has',
        '  strictly positive signed volume. Components are separate closed manufacturing',
        '  parts with intentional intersections at joints; these are rendering assemblies,',
        '  not Boolean-unioned solids for fabrication. Self-intersection is not exhaustively tested.',
        '- The cup, mast tubes and spool bore have modeled interior walls; their visible',
        '  openings are not missing surface polygons. The slab is an open-top tray with',
        '  an interior floor below Z=0, and no face across the LED aperture at Z=0.',
        '  The emitter is a closed, 2 mm thick tilted plate. Both pass full closure checks',
        '  even though the spec permits exceptions for those files.',
        '- Main exposed edges have 2–4 mm chamfers. Thin LEDs, emitter edges, tube walls,',
        '  small fasteners and probe tips use proportionally smaller chamfers so the',
        '  bevel does not consume the part. Rounded surfaces carry smooth vertex normals;',
        '  geometric creases above 30 degrees use separate normal vertices.',
        '- The lens disc has its exact top-centre vertex at (0, 0, 0.075), with seven',
        '  lenses underneath. The six collar LEDs and all other split material meshes',
        '  retain their parent assembly origins. Their bounds need not start at Z=0.',
        '- The eight magazine slots have measured 30 mm openings on 110 mm centres.',
        '  The magazine body is X=−0.25…0, Y=−0.5…0.5; LEDs protrude 2 mm in front.',
        '- The inner mast tube is Ø38 mm; its clamp is Ø90 mm, explaining the larger',
        '  total bounding box. The outer tube bore is Ø39.4 mm, leaving sliding clearance.',
        '  The clamp socket is Ø71 mm for the slab’s Ø70 mm connector.',
        '- Seven intentionally damaged mesh fixtures exercise validator rejection:',
        '  missing face, duplicate face, degenerate face, non-unit normal, reversed face,',
        '  wholly inward shell and exceeded triangle budget.',
        '',
        '## Visual review and corrections',
        '',
        'All 34 individual views were inspected. The side views supplement the three-quarter',
        'front views; the slab, lens array, emitter and carriage use a view from below to',
        'expose the surfaces their reviewers need to see. Six contact sheets contain the',
        'same views. Two additional clamp close-ups show its shoulder and four prongs.',
        '',
        'The first geometry pass was watertight but missed two assembly defects: the inner',
        'mast clamp ring was disconnected from the tube, and the door had gaps between its',
        'slats. Ray-intersection checks reproduced both failures before the generator was',
        'changed. The clamp now has an annular shoulder connecting it to the tube; a thin',
        'back seal closes the door gaps while retaining the horizontal ribs. Both final',
        'models pass the added checks and were rendered and inspected again. Initial',
        'reports and before-images remain in `out/m24-models/` for comparison.',
        '',
        '| Review sheet | Models |',
        '|---|---|',
    ]
    names=list(SPECS)
    for start in range(0,len(names),3):
        sheet=f'review-{start//3+1:02}.png'
        lines.append(f"| [{sheet}](../../../out/m24-models/{sheet}) | {', '.join(names[start:start+3])} |")
    lines += [
        '',
        'Individual previews: `out/m24-models/<model>-front.png` and `<model>-side.png`.',
        'Clamp detail: `gantry_mast_inner-clamp-top.png` and `gantry_mast_inner-clamp-bottom.png`.',
        'Final reports: `out/m24-models/validation.txt` and `validation.json`.',
        '',
        '## Scope and remaining work',
        '',
        'No requested model is missing. Game loading, room placement and in-game material',
        'appearance were not tested: this task permits only asset and preview-output writes.',
        'The previews use neutral inspection lighting, with cyan tint for emitter files;',
        'no such colours or material settings are embedded in the OBJ files. No changes',
        'were made to game code, tests, `lab.toml` or plans, and nothing was staged or committed.',
        '',
    ]
    (ROOT/'README.md').write_text('\n'.join(lines),encoding='utf-8')

if __name__=='__main__': main()
