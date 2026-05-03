#!/usr/bin/env python3
"""Per-event nuclear decay scheme PDFs (NNDC/ENSDF style).

For a run directory data/<timestamp>/, writes one single-page PDF per fission
event to plots/<timestamp>_plots/decay-schemes/. Each PDF is a TikZ-rendered
decay scheme covering both fragment chains: one vertical Z-column per nuclide,
horizontal level lines for nuclear ground states, diagonal arrows for
beta/alpha transitions between adjacent Z-columns, and vertical dashed arrows
for gamma de-excitation cascades inside a column.

Rendering pipeline: Python computes (columns, levels, transitions) from the
truth record, fills decay_scheme_template.tex.j2 via Jinja2, and shells out
to pdflatex to produce the PDF. Requires pdflatex on $PATH (TeX Live/MacTeX).

Delta electrons and other EM-transport secondaries are excluded
(creator_process must be in {primary, nFissionHP, RadioactiveDecay}).

Visual cleanup rules applied to every chain before rendering:

  1. Per-step emission filtering — drop entries with KE below
     EMISSION_KE_FLOOR_MEV (5 keV). These are internal-conversion / Auger
     electrons Geant4 emits as part of a nuclear decay step but they have
     no information value at decay-scheme scale.
  2. Isomer coalesce — merge consecutive same-(Z,A) chain entries (Geant4's
     IT cascade is recorded as several "Pr150 → Pr150" steps; collapse them
     into a single γ cascade in the parent column).
  3. γ-cascade collapse — every decay step's γ emissions render as ONE
     dashed arrow from the populated excited level down to the daughter
     ground, labelled "γ × N: total MeV" (or "γ: E_γ MeV" if N == 1).
     Geant4 represents the cascade as N sibling photons of the same
     RadioactiveDecay vertex, but the per-γ energies are rarely the
     actionable information at decay-scheme scale (the cumulative cascade
     energy = excited-state population energy is what the reader cares
     about, and that is annotated as E* on the topmost excited level).

After (2), no chain contains repeated consecutive same-(Z,A) ion entries.
After (3), each decay step contributes at most one γ arrow to the diagram.

Layout: within-column levels are pushed apart to a minimum vertical gap
(MIN_LEVEL_GAP_CM). This makes the y-axis nominal rather than strictly
proportional to cumulative Q; the precise cumulative-Q value is annotated
on each level line.
"""
from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path

import pandas as pd
from jinja2 import Environment, FileSystemLoader, StrictUndefined

# ── module-level constants ────────────────────────────────────────────────────

NUCLEAR_PROCESSES = {"primary", "nFissionHP", "RadioactiveDecay"}
ION_NAME_RE = re.compile(r"^([A-Z][a-z]?)(\d+)$")

# Visual-cleanup thresholds (see module docstring)
EMISSION_KE_FLOOR_MEV = 0.005   # drop emissions below 5 keV (IC / Auger noise)

# Layout
COLUMN_SPACING_CM = 3.2
LEVEL_HALF_W = 0.72         # half-width of level lines in cm
Y_SCALE = 0.70              # cm per MeV of cumulative Q (provisional, before
                            # MIN_LEVEL_GAP_CM enforcement)
GAMMA_X_OFFSET = 0.45       # cm right of level centre for vertical γ arrows
MIN_LEVEL_GAP_CM = 0.55     # minimum vertical gap between adjacent levels in
                            # the same column (post-spacing enforcement makes
                            # the y-axis nominal, not strictly linear in MeV)

# Cross-column transition labels: place closer to the source end of the arrow
# (parent column, less crowded) rather than at the midpoint (which lands over
# the busy daughter column).
CROSS_COL_LABEL_POS = 0.32

# Driver-emission detail: cap at this many lines per arrow label to prevent
# pathological cases (post-EMISSION_KE_FLOOR_MEV, this should rarely bite).
MAX_DRIVER_LINES = 3

# Explicit kind → TeX header mapping. Caller must use one of these keys for
# any cross-column transition; KeyError surfaces a programming bug instead of
# silently falling back to the kind name as a literal label.
KIND_TO_HEAD: dict[str, str] = {
    "betam": r"$\beta^-$",
    "betap": r"$\beta^+$",
    "alpha": r"$\alpha$",
}

ELEMENT_TO_Z: dict[str, int] = {
    "H": 1, "He": 2, "Li": 3, "Be": 4, "B": 5, "C": 6, "N": 7, "O": 8,
    "F": 9, "Ne": 10, "Na": 11, "Mg": 12, "Al": 13, "Si": 14, "P": 15,
    "S": 16, "Cl": 17, "Ar": 18, "K": 19, "Ca": 20, "Sc": 21, "Ti": 22,
    "V": 23, "Cr": 24, "Mn": 25, "Fe": 26, "Co": 27, "Ni": 28, "Cu": 29,
    "Zn": 30, "Ga": 31, "Ge": 32, "As": 33, "Se": 34, "Br": 35, "Kr": 36,
    "Rb": 37, "Sr": 38, "Y": 39, "Zr": 40, "Nb": 41, "Mo": 42, "Tc": 43,
    "Ru": 44, "Rh": 45, "Pd": 46, "Ag": 47, "Cd": 48, "In": 49, "Sn": 50,
    "Sb": 51, "Te": 52, "I": 53, "Xe": 54, "Cs": 55, "Ba": 56, "La": 57,
    "Ce": 58, "Pr": 59, "Nd": 60, "Pm": 61, "Sm": 62, "Eu": 63, "Gd": 64,
    "Tb": 65, "Dy": 66, "Ho": 67, "Er": 68, "Tm": 69, "Yb": 70, "Lu": 71,
    "Hf": 72, "Ta": 73, "W": 74, "Re": 75, "Os": 76, "Ir": 77, "Pt": 78,
    "Au": 79, "Hg": 80, "Tl": 81, "Pb": 82, "Bi": 83, "Po": 84, "At": 85,
    "Rn": 86, "Fr": 87, "Ra": 88, "Ac": 89, "Th": 90, "Pa": 91, "U": 92,
}


# ── helpers ────────────────────────────────────────────────────────────────────

def find_repo_root(start: Path) -> Path:
    for p in [start, *start.parents]:
        if (p / "nuclear-fission.cc").is_file():
            return p
    raise RuntimeError(
        f"Could not find repo root (no nuclear-fission.cc found above {start})"
    )


def parse_ion(particle: str) -> tuple[str, int] | None:
    """Return (element_symbol, A) or None if not an ion name."""
    m = ION_NAME_RE.match(particle)
    return (m.group(1), int(m.group(2))) if m else None


def is_ion(particle: str) -> bool:
    return ION_NAME_RE.match(particle) is not None


def ion_z(particle: str) -> int | None:
    p = parse_ion(particle)
    return ELEMENT_TO_Z.get(p[0]) if p else None


def ion_za(particle: str) -> tuple[int, int] | None:
    p = parse_ion(particle)
    if not p or p[0] not in ELEMENT_TO_Z:
        return None
    return (ELEMENT_TO_Z[p[0]], p[1])


def build_decay_tree(event_df: pd.DataFrame) -> dict[int, list[int]]:
    tree: dict[int, list[int]] = {}
    for _, r in event_df.iterrows():
        tree.setdefault(int(r["parent_track_id"]), []).append(int(r["track_id"]))
    return tree


def walk_chain(
    event_df: pd.DataFrame,
    fragment_tid: int,
    tree: dict[int, list[int]],
    by_id: dict[int, pd.Series],
) -> list[dict]:
    """Walk one fragment's decay chain.

    Each entry: {ion_name, ion_track_id, decays, decay_q_mev,
                 emissions [(particle, ke_mev), ...], daughter_track_id}.
    """
    result: list[dict] = []
    cur: int | None = fragment_tid
    visited: set[int] = set()

    while cur is not None and cur not in visited:
        visited.add(cur)
        if cur not in by_id:
            break
        cur_row = by_id[cur]
        cur_name = str(cur_row["particle"])

        kids = tree.get(cur, [])
        decay_children = [
            by_id[k] for k in kids
            if k in by_id and str(by_id[k]["creator_process"]) == "RadioactiveDecay"
        ]

        if not decay_children:
            result.append({
                "ion_name": cur_name, "ion_track_id": cur,
                "decays": False, "decay_q_mev": 0.0,
                "emissions": [], "daughter_track_id": None,
            })
            break

        ion_kids = [c for c in decay_children if is_ion(str(c["particle"]))]
        daughter_id = int(ion_kids[0]["track_id"]) if ion_kids else None

        emissions: list[tuple[str, float]] = []
        q_total = 0.0
        for c in decay_children:
            cke = float(c["initial_KE_MeV"])
            q_total += cke
            if daughter_id is not None and int(c["track_id"]) == daughter_id:
                continue  # recoil counted in Q but not listed as emission
            emissions.append((str(c["particle"]), cke))

        result.append({
            "ion_name": cur_name, "ion_track_id": cur,
            "decays": True, "decay_q_mev": q_total,
            "emissions": emissions, "daughter_track_id": daughter_id,
        })
        cur = daughter_id

    return result


# ── chain post-processing (filter + coalesce) ─────────────────────────────────

def filter_chain_emissions(chain: list[dict]) -> list[dict]:
    """Drop per-step emission entries whose KE is below EMISSION_KE_FLOOR_MEV.

    Eliminates the 'β⁻: 0.000' clutter that comes from Geant4's internal-
    conversion / Auger electrons being included in the RadioactiveDecay
    children. The total step Q (decay_q_mev) is unchanged — only the visible
    list of emissions shrinks.
    """
    out: list[dict] = []
    for step in chain:
        if not step["decays"]:
            out.append(step)
            continue
        filtered = [
            (p, ke) for (p, ke) in step["emissions"]
            if ke >= EMISSION_KE_FLOOR_MEV
        ]
        out.append({**step, "emissions": filtered})
    return out


def coalesce_isomers(chain: list[dict]) -> list[dict]:
    """Merge consecutive same-(Z,A) chain entries.

    Whenever step i and step i+1 have the same (Z, A) as their ion AND step
    i is a γ-only step (no β/α emission) AND both decay, fold step i's γ
    emissions and Q into step i+1's. This collapses Geant4's IT cascade
    (e.g. 'Pr150 → Pr150 → Pr150 → Nd150') into a single decay step in the
    chain that carries the cumulative γ cascade plus whatever drove the
    eventual β/α transition out of the column.

    If step i+1 is terminal (decays=False), no merge happens — step i
    still draws as an IT in its column via the γ-cascade-in-daughter logic
    in build_diagram (skipping the cross-column header), and step i+1
    draws as the terminal ground level.
    """
    if len(chain) < 2:
        return chain

    result: list[dict] = [dict(s) for s in chain]
    i = 0
    while i + 1 < len(result):
        cur, nxt = result[i], result[i + 1]
        if not (cur.get("decays") and nxt.get("decays")):
            i += 1
            continue
        cur_za = ion_za(cur["ion_name"])
        nxt_za = ion_za(nxt["ion_name"])
        if cur_za is None or nxt_za is None or cur_za != nxt_za:
            i += 1
            continue
        # cur must be γ-only (or empty after emission filtering)
        cur_kinds = {p for p, _ in cur["emissions"]}
        if not cur_kinds.issubset({"gamma"}):
            i += 1
            continue
        # Merge cur into nxt
        result[i + 1] = {
            **nxt,
            "ion_name": cur["ion_name"],
            "ion_track_id": cur["ion_track_id"],
            "emissions": list(cur["emissions"]) + list(nxt["emissions"]),
            "decay_q_mev": cur["decay_q_mev"] + nxt["decay_q_mev"],
        }
        del result[i]
        # Don't advance i — re-check possibility of further merge
    return result


def classify_decay(emissions: list[tuple[str, float]]) -> str:
    """Classify a decay step from its (filtered) emission list."""
    names = {n for n, _ in emissions}
    if "alpha" in names:
        return "alpha"
    if "e-" in names:
        return "betam"
    if "e+" in names:
        return "betap"
    return "gammaarrow"


# ── diagram data model ─────────────────────────────────────────────────────────

@dataclass
class Column:
    z: int
    a: int
    symbol: str
    # Filled by lay_out:
    x: float = 0.0
    header_y: float = 0.0


@dataclass
class Level:
    col_id: int                     # index into columns list (pre-sort key)
    y_mev: float                    # provisional vertical position in raw MeV
    right_label: str = ""           # cumulative-Q text shown to right
    excited_label: str = ""         # E* text shown above (for excited states)
    terminal_label: str = ""        # "(stable / no decay in sim window)" text
    # Filled by lay_out:
    x_left: float = 0.0
    x_right: float = 0.0
    y: float = 0.0                  # final cm coordinate
    level_idx: int = -1             # back-reference for transition routing


@dataclass
class RawTransition:
    """Transition described in column / level / MeV coordinates (pre-layout).

    src_level_idx and dst_level_idx point at indices in the levels list
    (set by build_diagram); lay_out uses them to look up the post-spacing
    y-coordinate so transitions stay attached after MIN_LEVEL_GAP_CM
    enforcement may have shifted levels around.
    """
    kind: str            # betam / betap / alpha / gammaarrow
    src_col_id: int
    src_level_idx: int   # -1 = use src_y_mev * Y_SCALE directly (no anchor)
    src_y_mev: float
    dst_col_id: int
    dst_level_idx: int   # -1 = use dst_y_mev * Y_SCALE directly
    dst_y_mev: float
    label: str = ""
    label_pos: float = 0.5


# ── chain → diagram reduction ──────────────────────────────────────────────────

def _format_driver_lines(drivers: list[tuple[str, float]]) -> list[str]:
    """Format the driver-emission detail lines for a cross-column arrow label.

    Combines β⁻ + ν̄_e (or β⁺ + ν_e) into a single line if both are present
    with non-trivial KE; otherwise lists them separately. Caps at
    MAX_DRIVER_LINES total lines.
    """
    by_kind: dict[str, list[float]] = {}
    for p, ke in drivers:
        by_kind.setdefault(p, []).append(ke)

    lines: list[str] = []

    # β⁻ + ν̄_e combined
    if "e-" in by_kind and "anti_nu_e" in by_kind:
        em_ke = max(by_kind["e-"])
        nu_ke = max(by_kind["anti_nu_e"])
        lines.append(
            rf"$\beta^- + \bar{{\nu}}_e$: {em_ke:.3f} + {nu_ke:.3f} MeV"
        )
        # Any additional e-'s (rare after filtering) on a separate line
        extras = sorted(by_kind["e-"], reverse=True)[1:]
        for ke in extras[:1]:
            lines.append(rf"$\beta^-$: {ke:.3f}")
    # β⁺ + ν_e combined
    elif "e+" in by_kind and "nu_e" in by_kind:
        em_ke = max(by_kind["e+"])
        nu_ke = max(by_kind["nu_e"])
        lines.append(
            rf"$\beta^+ + \nu_e$: {em_ke:.3f} + {nu_ke:.3f} MeV"
        )
    else:
        # Fall back to per-particle listing, sorted by KE descending
        sorted_drivers = sorted(drivers, key=lambda x: -x[1])
        for p, ke in sorted_drivers:
            if p == "e-":
                lines.append(rf"$\beta^-$: {ke:.3f}")
            elif p == "e+":
                lines.append(rf"$\beta^+$: {ke:.3f}")
            elif p == "alpha":
                lines.append(rf"$\alpha$: {ke:.3f}")
            elif p == "anti_nu_e":
                lines.append(rf"$\bar{{\nu}}_e$: {ke:.3f}")
            elif p == "nu_e":
                lines.append(rf"$\nu_e$: {ke:.3f}")
            else:
                lines.append(rf"{p}: {ke:.3f}")

    return lines[:MAX_DRIVER_LINES]


def build_diagram(
    chains: list[list[dict]],
) -> tuple[list[Column], list[Level], list[RawTransition]]:
    """Convert one or two fragment chains into columns, levels, and transitions.

    Columns: one-per-(Z, A) nuclide. Levels sit at provisional y = -cumulative_Q
    in MeV (lay_out converts to cm and applies minimum-spacing enforcement).
    Cross-column β/α arrows + γ-cascade arrows are returned as RawTransitions
    with src_level_idx / dst_level_idx anchors so lay_out can re-route them
    after spacing tweaks.

    γ-only IT steps (kind == 'gammaarrow') do NOT emit a cross-column header
    transition; their information is fully captured by the in-column γ cascade
    arrows. This avoids the zero-length / mis-labeled artefact that would
    otherwise appear in the diagram.
    """
    columns: list[Column] = []
    col_by_za: dict[tuple[int, int], int] = {}

    def get_or_add_column(sym: str, a: int) -> int:
        z = ELEMENT_TO_Z[sym]
        key = (z, a)
        if key not in col_by_za:
            idx = len(columns)
            col_by_za[key] = idx
            columns.append(Column(z=z, a=a, symbol=sym))
        return col_by_za[key]

    levels: list[Level] = []
    transitions: list[RawTransition] = []

    def add_level(lv: Level) -> int:
        idx = len(levels)
        lv.level_idx = idx
        levels.append(lv)
        return idx

    for chain in chains:
        cumulative_q = 0.0

        for i, step in enumerate(chain):
            parsed = parse_ion(step["ion_name"])
            if parsed is None:
                print(f"warning: skipping non-ion {step['ion_name']!r}", file=sys.stderr)
                break
            sym, a = parsed
            if sym not in ELEMENT_TO_Z:
                print(f"warning: unknown element {sym!r}, skipping", file=sys.stderr)
                break

            col_id = get_or_add_column(sym, a)
            ion_y = -cumulative_q

            ion_level_idx = add_level(Level(
                col_id=col_id,
                y_mev=ion_y,
                right_label=f"{cumulative_q:.3f}\\,MeV",
                terminal_label=(r"(stable / no decay in sim window)"
                                if not step["decays"] else ""),
            ))

            if not step["decays"]:
                break

            # ── look up daughter (next chain entry) ────────────────────────────
            if i + 1 >= len(chain):
                cumulative_q += step["decay_q_mev"]
                continue

            dname = chain[i + 1]["ion_name"]
            dparsed = parse_ion(dname)
            if dparsed is None or dparsed[0] not in ELEMENT_TO_Z:
                print(f"warning: bad daughter {dname!r}, skipping step", file=sys.stderr)
                cumulative_q += step["decay_q_mev"]
                continue

            dsym, da = dparsed
            dcol_id = get_or_add_column(dsym, da)

            # ── classify and split emissions ───────────────────────────────────
            kind = classify_decay(step["emissions"])
            gammas = sorted(
                [(ke, p) for p, ke in step["emissions"] if p == "gamma"],
                reverse=True,  # highest-energy γ first
            )
            drivers = [(p, ke) for p, ke in step["emissions"] if p != "gamma"]
            sum_gamma = sum(ke for ke, _ in gammas)
            q_total = step["decay_q_mev"]
            daughter_ground_y = ion_y - q_total

            # Where the β/α arrow lands in the daughter column (excited state
            # populated by the β/α, ground if no γ cascade):
            arrow_dst_y = daughter_ground_y + sum_gamma

            # ── γ cascade as a single collapsed arrow ──────────────────────────
            # Geant4 emits all γs of a decay step as siblings of the same
            # RadioactiveDecay vertex; we render them as ONE dashed arrow from
            # the populated excited level (top) down to the daughter ground
            # level. The label carries (count, total energy). This eliminates
            # the per-γ midway-label collisions that plague γ-rich columns.
            #
            # For IT steps (kind == 'gammaarrow'), the daughter column equals
            # the parent column; the collapsed cascade renders inside that
            # column. The cross-column β/α header below is then skipped.
            cascade_top_level_idx = -1
            if gammas:
                # One excited level at the populated state (top of cascade)
                cascade_top_level_idx = add_level(Level(
                    col_id=dcol_id,
                    y_mev=arrow_dst_y,
                    excited_label=f"$E^* = {sum_gamma:.3f}$",
                ))
                n_g = len(gammas)
                if n_g == 1:
                    g_label = rf"$\gamma$: {sum_gamma:.3f} MeV"
                else:
                    g_label = rf"$\gamma \times {n_g}$: {sum_gamma:.3f} MeV"
                transitions.append(RawTransition(
                    kind="gammaarrow",
                    src_col_id=dcol_id,
                    src_level_idx=cascade_top_level_idx,
                    src_y_mev=arrow_dst_y,
                    dst_col_id=dcol_id,
                    dst_level_idx=-1,           # next chain iteration adds the daughter ground level
                    dst_y_mev=daughter_ground_y,
                    label=g_label,
                    label_pos=0.5,
                ))

            # ── cross-column β/α arrow ─────────────────────────────────────────
            # Skip cross-column header for γ-only (IT) steps. After coalesce,
            # IT-only steps should be rare, but a fragment that begins with an
            # IT relaxation will still hit this branch.
            if kind != "gammaarrow":
                head = KIND_TO_HEAD[kind]   # KeyError on unknown kind = caller bug
                label_lines = [f"{head}, $Q={q_total:.3f}$"]
                label_lines.extend(_format_driver_lines(drivers))

                transitions.append(RawTransition(
                    kind=kind,
                    src_col_id=col_id,
                    src_level_idx=ion_level_idx,
                    src_y_mev=ion_y,
                    dst_col_id=dcol_id,
                    dst_level_idx=cascade_top_level_idx if gammas else -1,
                    dst_y_mev=arrow_dst_y,
                    label=r"\\".join(label_lines),
                    label_pos=CROSS_COL_LABEL_POS,
                ))

            cumulative_q += q_total

    return columns, levels, transitions


# ── layout: MeV coordinates → TikZ cm coordinates ─────────────────────────────

def _tex_text(s: str) -> str:
    """Escape characters that are special in LaTeX text mode."""
    return (s
            .replace("\\", r"\textbackslash{}")
            .replace("_", r"\_")
            .replace("#", r"\#")
            .replace("%", r"\%")
            .replace("&", r"\&")
            .replace("^", r"\^{}")
            .replace("~", r"\textasciitilde{}")
            )


def _enforce_min_level_gap(
    levels: list[Level],
    columns: list[Column],
) -> None:
    """In each column, ensure adjacent levels are at least MIN_LEVEL_GAP_CM apart.

    Walks levels top-to-bottom (highest y first); pushes lower levels further
    down whenever the gap is too small. This makes the y-axis nominal, not
    strictly proportional to cumulative Q. Per-level cumulative-Q labels carry
    the precise quantitative information.
    """
    for col_id in range(len(columns)):
        col_levels = sorted(
            [lv for lv in levels if lv.col_id == col_id],
            key=lambda lv: -lv.y,
        )
        for prev, cur in zip(col_levels, col_levels[1:]):
            gap = prev.y - cur.y
            if gap < MIN_LEVEL_GAP_CM:
                cur.y -= (MIN_LEVEL_GAP_CM - gap)


def lay_out(
    columns: list[Column],
    levels: list[Level],
    transitions: list[RawTransition],
    *,
    event_id: int,
    timestamp: str,
    fragment_names: list[str],
) -> dict:
    """Convert column/level/transition objects into the flat dict the Jinja2
    template expects. Returns the complete template context."""

    # Sort columns by Z (ascending), remap col_id in levels and transitions.
    sort_order = sorted(range(len(columns)), key=lambda i: (columns[i].z, columns[i].a))
    remap = {old: new for new, old in enumerate(sort_order)}
    new_columns = [columns[i] for i in sort_order]

    for new_i, c in enumerate(new_columns):
        c.x = new_i * COLUMN_SPACING_CM

    # Provisional y in cm (Y_SCALE × MeV); resolve column x for each level.
    for lv in levels:
        lv.col_id = remap[lv.col_id]
        cx = new_columns[lv.col_id].x
        lv.x_left = cx - LEVEL_HALF_W
        lv.x_right = cx + LEVEL_HALF_W
        lv.y = lv.y_mev * Y_SCALE

    # Enforce minimum within-column spacing (post-pass).
    _enforce_min_level_gap(levels, new_columns)

    # Now route transitions using each transition's level-index anchors so the
    # endpoints stay attached to (possibly shifted) level lines.
    trans_out: list[dict] = []
    for t in transitions:
        if t.src_level_idx >= 0:
            src_y = levels[t.src_level_idx].y
        else:
            src_y = t.src_y_mev * Y_SCALE
        if t.dst_level_idx >= 0:
            dst_y = levels[t.dst_level_idx].y
        else:
            dst_y = t.dst_y_mev * Y_SCALE

        src_col = new_columns[remap[t.src_col_id]]
        dst_col = new_columns[remap[t.dst_col_id]]

        if t.kind == "gammaarrow":
            # Vertical inside the daughter column, slightly right of centre
            x = dst_col.x + GAMMA_X_OFFSET
            trans_out.append({
                "kind": "gammaarrow",
                "src_x": x, "src_y": src_y,
                "dst_x": x, "dst_y": dst_y,
                "label": t.label, "label_pos": t.label_pos,
            })
        else:
            # Diagonal cross-column arrow
            going_right = remap[t.dst_col_id] > remap[t.src_col_id]
            if going_right:
                sx = src_col.x + LEVEL_HALF_W
                dx = dst_col.x - LEVEL_HALF_W
            else:
                sx = src_col.x - LEVEL_HALF_W
                dx = dst_col.x + LEVEL_HALF_W
            trans_out.append({
                "kind": t.kind,
                "src_x": sx, "src_y": src_y,
                "dst_x": dx, "dst_y": dst_y,
                "label": t.label, "label_pos": t.label_pos,
            })

    # Column headers sit 1 cm above the topmost level
    all_y_cm = [lv.y for lv in levels] or [0.0]
    top_y_cm = max(all_y_cm)
    bot_y_cm = min(all_y_cm)
    for c in new_columns:
        c.header_y = top_y_cm + 1.0

    # Title / subtitle
    title = f"Event {event_id} --- nuclear decay scheme"
    frag_str = "  $\\cdot$  ".join(
        f"fragment {chr(ord('A') + k)}: {_tex_text(n)}"
        for k, n in enumerate(fragment_names[:2])
    )
    subtitle = (
        f"run {_tex_text(timestamp)}"
        + (f"  $\\cdot$  {frag_str}" if frag_str else "")
        + r"  $\cdot$  $\delta$-electrons excluded"
        + r"  $\cdot$  $E^* < 5$ keV emissions filtered"
    )

    yaxis_x = new_columns[0].x - 2.0 if new_columns else -2.0

    return {
        "title": title,
        "subtitle": subtitle,
        "title_x": yaxis_x + 0.5,
        "title_y": top_y_cm + 2.4,
        "subtitle_x": yaxis_x + 0.5,
        "subtitle_y": top_y_cm + 1.9,
        "columns": [
            {"x": c.x, "header_y": c.header_y, "symbol": c.symbol, "a": c.a}
            for c in new_columns
        ],
        "levels": [
            {
                "x_left": lv.x_left, "x_right": lv.x_right, "y": lv.y,
                "right_label": lv.right_label,
                "excited_label": lv.excited_label,
                "terminal_label": lv.terminal_label,
            }
            for lv in levels
        ],
        "transitions": trans_out,
        "yaxis_x": yaxis_x,
        "yaxis_top": top_y_cm + 0.3,
        "yaxis_bot": bot_y_cm - 0.4,
    }


# ── TeX rendering and pdflatex compile ────────────────────────────────────────

def render_tex(template_path: Path, context: dict) -> str:
    env = Environment(
        loader=FileSystemLoader(str(template_path.parent)),
        undefined=StrictUndefined,
        trim_blocks=False,
        lstrip_blocks=False,
    )
    return env.get_template(template_path.name).render(**context)


def compile_pdf(tex_source: str, out_pdf: Path, *, job_name: str) -> None:
    if not shutil.which("pdflatex"):
        raise RuntimeError(
            "pdflatex not found on $PATH. "
            "Install MacTeX (/Library/TeX/texbin) or TeX Live and add its bin/ "
            "directory to $PATH before running this script."
        )
    with tempfile.TemporaryDirectory() as td:
        tex_file = Path(td) / f"{job_name}.tex"
        tex_file.write_text(tex_source, encoding="utf-8")
        try:
            subprocess.run(
                [
                    "pdflatex",
                    "-interaction=nonstopmode",
                    "-halt-on-error",
                    f"-output-directory={td}",
                    str(tex_file),
                ],
                check=True,
                capture_output=True,
            )
        except subprocess.CalledProcessError as e:
            out_pdf.parent.mkdir(parents=True, exist_ok=True)
            (out_pdf.parent / f"{job_name}.tex").write_text(tex_source)
            (out_pdf.parent / f"{job_name}.log").write_bytes(e.stdout or b"")
            raise RuntimeError(
                f"pdflatex failed for {job_name}; "
                f"saved .tex and .log to {out_pdf.parent}"
            ) from e
        produced = Path(td) / f"{job_name}.pdf"
        if not produced.is_file():
            raise RuntimeError(f"pdflatex ran but did not produce {produced}")
        out_pdf.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(produced, out_pdf)


# ── main ──────────────────────────────────────────────────────────────────────

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "timestamp",
        help="Run directory name under data/, e.g. 20260430T052100",
    )
    args = parser.parse_args()

    repo_root = find_repo_root(Path(__file__).resolve().parent)
    run_dir = repo_root / "data" / args.timestamp
    truth_csv = run_dir / "truth-record.csv"
    events_csv = run_dir / "events-truth.csv"

    for p, label in [(run_dir, "run directory"),
                     (truth_csv, "truth-record.csv"),
                     (events_csv, "events-truth.csv")]:
        if not p.exists():
            print(f"error: {label} not found: {p}", file=sys.stderr)
            return 1

    template_path = Path(__file__).resolve().parent / "decay_scheme_template.tex.j2"
    if not template_path.is_file():
        print(f"error: template not found: {template_path}", file=sys.stderr)
        return 1

    out_dir = repo_root / "plots" / f"{args.timestamp}_plots" / "decay-schemes"
    out_dir.mkdir(parents=True, exist_ok=True)

    df = pd.read_csv(truth_csv)
    df_nuclear = df[df["creator_process"].isin(NUCLEAR_PROCESSES)]
    events_df = pd.read_csv(events_csv, comment="#")

    n_written = n_failed = 0
    for _, row in events_df.iterrows():
        event_id = int(row["event_id"])
        event_df = df_nuclear[df_nuclear["event_id"] == event_id]
        if event_df.empty:
            print(f"warning: event {event_id}: no nuclear-process tracks; skipping",
                  file=sys.stderr)
            continue

        by_id = {int(r["track_id"]): r for _, r in event_df.iterrows()}
        tree = build_decay_tree(event_df)

        # Fragment tracks: nFissionHP children that are ions
        frag_rows = event_df[
            (event_df["creator_process"] == "nFissionHP")
            & event_df["particle"].astype(str).map(is_ion)
        ]
        fragment_tids = [int(r["track_id"]) for _, r in frag_rows.iterrows()][:2]

        # Walk → filter emissions → coalesce isomer cascades
        chains: list[list[dict]] = []
        for tid in fragment_tids:
            chain = walk_chain(event_df, tid, tree, by_id)
            chain = filter_chain_emissions(chain)
            chain = coalesce_isomers(chain)
            chains.append(chain)
        fragment_names = [c[0]["ion_name"] for c in chains if c]

        columns, levels, transitions = build_diagram(chains)
        if not levels:
            print(f"warning: event {event_id}: diagram is empty; skipping",
                  file=sys.stderr)
            continue

        context = lay_out(
            columns, levels, transitions,
            event_id=event_id,
            timestamp=args.timestamp,
            fragment_names=fragment_names,
        )

        job = f"event_{event_id}"
        out_pdf = out_dir / f"{job}_decay_scheme.pdf"
        try:
            compile_pdf(render_tex(template_path, context), out_pdf, job_name=job)
            n_written += 1
        except RuntimeError as e:
            print(f"error: event {event_id}: {e}", file=sys.stderr)
            n_failed += 1

    status = f"wrote {n_written} decay-scheme PDFs to {out_dir}"
    if n_failed:
        status += f" ({n_failed} failed)"
    print(status)
    return 0 if n_failed == 0 else 2


if __name__ == "__main__":
    sys.exit(main())
