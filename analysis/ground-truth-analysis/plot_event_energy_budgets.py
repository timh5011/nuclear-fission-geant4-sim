#!/usr/bin/env python3
"""Per-event 3-page energy-budget PDFs.

For a run directory data/<timestamp>/, writes one multi-page PDF per fission
event to plots/<timestamp>_plots/energy-budgets/. Each PDF has three pages:

  page 1: Total-energy Sankey (ribbon width = KE + mc^2, linear scale).
          Chronological parent-child topology of the decay tree.
  page 2: Fission energy-partition bar (textbook ENDF-style channels).
  page 3: Nuclear decay scheme (NNDC-style level diagram per fragment).

Delta electrons and other EM transport secondaries are excluded from all
three diagrams (creator_process filtered to primary/nFissionHP/
RadioactiveDecay).
"""
from __future__ import annotations

import argparse
import re
import sys
import tempfile
from pathlib import Path

import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
import plotly.graph_objects as go
from pypdf import PdfWriter

NUCLEAR_PROCESSES = {"primary", "nFissionHP", "RadioactiveDecay"}
ION_NAME_RE = re.compile(r"^[A-Z][a-z]?\d+$")
ION_DIGITS_RE = re.compile(r"\d+")

# Color palette by particle kind
COLOR_SOURCE = "#222222"
COLOR_ION = "#b15928"
COLOR_NEUTRON = "#1f78b4"
COLOR_GAMMA = "#33a02c"
COLOR_BETA = "#e31a1c"
COLOR_ALPHA = "#ff7f00"
COLOR_NU = "#a6cee3"
COLOR_OTHER = "#999999"

# ENDF/B-VIII.0 thermal U-235 reference partition (MeV)
ENDF_REFERENCE = {
    "Fragment KE":  169.1,
    "Prompt n KE":    4.79,
    "Prompt γ":       6.97,
    "β KE":           6.98,
    "β-delayed γ":    6.30,
    "Anti-νₑ":        8.75,
}
ENDF_TOTAL = sum(ENDF_REFERENCE.values())  # 202.89


# ---------- low-level helpers ----------

def find_repo_root(start: Path) -> Path:
    for p in [start, *start.parents]:
        if (p / "nuclear-fission.cc").is_file():
            return p
    raise RuntimeError(
        f"Could not find repo root (no nuclear-fission.cc found above {start})"
    )


def pdg_to_z(pdg: int) -> int | None:
    if pdg < 1_000_000_000:
        return None
    return (pdg // 10_000) % 1000


def pdg_to_a(pdg: int) -> int | None:
    if pdg < 1_000_000_000:
        return None
    return (pdg // 10) % 1000


def is_ion(particle: str) -> bool:
    return bool(ION_NAME_RE.match(particle))


def rest_mass_mev(particle: str) -> float:
    """Rough rest-mass-energy in MeV. Ion masses are A * u (atomic mass unit
    ~931.494 MeV/c^2) -- accurate to ~0.1% which is well below visual scale."""
    if particle == "neutron":
        return 939.565
    if particle in ("gamma", "anti_nu_e"):
        return 0.0
    if particle in ("e-", "e+"):
        return 0.511
    if particle == "alpha":
        return 3727.379
    if is_ion(particle):
        m = ION_DIGITS_RE.search(particle)
        if m:
            return int(m.group()) * 931.494
    return 0.0


def color_for(particle: str) -> str:
    if particle == "neutron":
        return COLOR_NEUTRON
    if particle == "gamma":
        return COLOR_GAMMA
    if particle in ("e-", "e+"):
        return COLOR_BETA
    if particle == "alpha":
        return COLOR_ALPHA
    if particle == "anti_nu_e":
        return COLOR_NU
    if is_ion(particle):
        return COLOR_ION
    return COLOR_OTHER


def display_name(particle: str) -> str:
    if particle == "gamma":
        return "γ"
    if particle == "anti_nu_e":
        return "anti-νₑ"
    return particle


def hex_to_rgba(hex_color: str, alpha: float) -> str:
    h = hex_color.lstrip("#")
    r, g, b = int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)
    return f"rgba({r},{g},{b},{alpha})"


# ---------- truth-tree analysis ----------

def compute_depths(event_df: pd.DataFrame) -> dict[int, int]:
    """Map track_id -> depth (0 = primary, 1 = nFissionHP children, etc.)."""
    depths: dict[int, int] = {}
    rows = event_df.sort_values("track_id")
    for _, r in rows.iterrows():
        tid = int(r["track_id"])
        ptid = int(r["parent_track_id"])
        if ptid == 0:
            depths[tid] = 0
        else:
            depths[tid] = depths.get(ptid, 0) + 1
    return depths


def build_decay_tree(event_df: pd.DataFrame) -> dict[int, list[int]]:
    """Map parent_track_id -> [child track_ids]."""
    tree: dict[int, list[int]] = {}
    for _, r in event_df.iterrows():
        ptid = int(r["parent_track_id"])
        tid = int(r["track_id"])
        tree.setdefault(ptid, []).append(tid)
    return tree


# ---------- page 1: total-energy Sankey ----------

def write_etotal_sankey(
    event_df: pd.DataFrame,
    *,
    event_id: int,
    timestamp: str,
    fragment_zs: tuple[int | None, int | None],
    out_path: Path,
) -> None:
    depths = compute_depths(event_df)
    max_depth = max(depths.values()) if depths else 1

    # Bucket key always includes depth so excited isomers (e.g. Y94* -> Y94 via
    # gamma de-excitation, where both are named "Y94" in the truth-record) get
    # distinct nodes at different columns instead of collapsing into one node
    # with a self-loop.
    def bucket_key(particle: str, depth: int) -> str:
        return f"{particle}@{depth}"

    def bucket_label(particle: str, depth: int) -> str:
        if is_ion(particle):
            return f"{particle} (gen {depth})"
        if depth == 1 and particle in ("neutron", "gamma"):
            return f"{display_name(particle)} (prompt)"
        return f"{display_name(particle)} (gen {depth})"

    # Build nodes (with width = sum E_total, count) and links from parent->child.
    # Node 0 reserved for "Fission" source.
    node_idx: dict[str, int] = {"_source_": 0}
    node_label: list[str] = [""]  # will fill later
    node_color: list[str] = [COLOR_SOURCE]
    node_x: list[float] = [0.001]
    node_width: list[float] = [0.0]  # sum of E_total
    node_count: list[int] = [0]

    def depth_to_x(d: int) -> float:
        if max_depth <= 0:
            return 0.5
        return 0.05 + 0.90 * d / max(max_depth, 1)

    def get_or_make_node(particle: str, depth: int) -> int:
        key = bucket_key(particle, depth)
        if key in node_idx:
            return node_idx[key]
        idx = len(node_label)
        node_idx[key] = idx
        node_label.append(bucket_label(particle, depth))
        node_color.append(color_for(particle))
        node_x.append(depth_to_x(depth))
        node_width.append(0.0)
        node_count.append(0)
        return idx

    # Pre-build track_id -> (particle, depth) for parent lookups
    track_info: dict[int, tuple[str, int]] = {}
    for _, r in event_df.iterrows():
        tid = int(r["track_id"])
        track_info[tid] = (str(r["particle"]), depths[tid])

    # Aggregate links
    link_value: dict[tuple[int, int], float] = {}
    link_count: dict[tuple[int, int], int] = {}

    for _, r in event_df.iterrows():
        tid = int(r["track_id"])
        ptid = int(r["parent_track_id"])
        particle = str(r["particle"])
        ke = float(r["initial_KE_MeV"])
        depth = depths[tid]
        creator = str(r["creator_process"])

        # Skip the primary itself (depth 0): negligible KE, not visualized
        if creator == "primary":
            continue

        e_total = ke + rest_mass_mev(particle)
        if e_total <= 0:
            continue

        child_idx = get_or_make_node(particle, depth)
        node_width[child_idx] += e_total
        node_count[child_idx] += 1

        if creator == "nFissionHP":
            parent_idx = 0  # the source node
            node_width[0] += e_total
            node_count[0] += 1
        elif creator == "RadioactiveDecay":
            if ptid not in track_info:
                continue
            p_part, p_depth = track_info[ptid]
            parent_idx = get_or_make_node(p_part, p_depth)
        else:
            continue

        key = (parent_idx, child_idx)
        link_value[key] = link_value.get(key, 0.0) + e_total
        link_count[key] = link_count.get(key, 0) + 1

    # Finalize source label
    node_label[0] = (
        f"Fission<br>{node_width[0]:,.0f} MeV<br>n = {node_count[0]}"
    )

    # Build full labels for non-source nodes
    for i in range(1, len(node_label)):
        # node_label[i] currently holds the bare bucket label
        node_label[i] = (
            f"{node_label[i]}<br>{node_width[i]:,.0f} MeV<br>n = {node_count[i]}"
        )

    sources, targets, values, link_labels, link_colors = [], [], [], [], []
    for (s, t), v in link_value.items():
        sources.append(s)
        targets.append(t)
        values.append(v)
        cnt = link_count[(s, t)]
        link_labels.append(f"{v:,.1f} MeV (n={cnt})")
        link_colors.append(hex_to_rgba(node_color[s], 0.4))

    z_a, z_b = fragment_zs
    z_str = f"  ·  fragments Z={z_a}+Z={z_b}" if z_a and z_b else ""
    title = (
        f"Event {event_id} energy budget — page 1 of 3 — Total-energy Sankey (KE + mc²)<br>"
        f"<sub>run {timestamp}{z_str}  ·  source width = {node_width[0]:,.0f} MeV "
        f"(dominated by ~{int(2 * 137 * 931.494 / 1000)} GeV fragment rest mass)<br>"
        f"width is linear; rest-mass dominance is intentional  ·  delta electrons excluded</sub>"
    )

    fig = go.Figure(data=[go.Sankey(
        arrangement="perpendicular",
        node=dict(
            pad=30,
            thickness=18,
            line=dict(color="black", width=0.5),
            label=node_label,
            color=node_color,
            x=node_x,
        ),
        link=dict(
            source=sources,
            target=targets,
            value=values,
            label=link_labels,
            color=link_colors,
        ),
    )])
    fig.update_layout(
        title=dict(text=title, x=0.02, xanchor="left", font=dict(size=12)),
        font=dict(size=10),
        margin=dict(l=20, r=20, t=100, b=20),
        width=1400,
        height=950,
    )
    fig.write_image(out_path, format="pdf", engine="kaleido")


# ---------- page 2: energy-partition bar ----------

def partition_channels(event_df: pd.DataFrame) -> list[tuple[str, float, str]]:
    """Returns list of (channel name, MeV, color) summing to total tracked KE."""
    nfh = event_df[event_df["creator_process"] == "nFissionHP"]
    rd = event_df[event_df["creator_process"] == "RadioactiveDecay"]

    nfh_ion_mask = nfh["particle"].astype(str).map(is_ion)
    rd_ion_mask = rd["particle"].astype(str).map(is_ion)

    fragment_ke = nfh.loc[nfh_ion_mask, "initial_KE_MeV"].sum()
    prompt_n = nfh.loc[nfh["particle"] == "neutron", "initial_KE_MeV"].sum()
    prompt_g = nfh.loc[nfh["particle"] == "gamma", "initial_KE_MeV"].sum()
    beta_ke = rd.loc[rd["particle"].isin(["e-", "e+"]), "initial_KE_MeV"].sum()
    delayed_g = rd.loc[rd["particle"] == "gamma", "initial_KE_MeV"].sum()
    nu = rd.loc[rd["particle"] == "anti_nu_e", "initial_KE_MeV"].sum()

    classified_total = float(fragment_ke + prompt_n + prompt_g + beta_ke + delayed_g + nu)
    total_ke = float(event_df["initial_KE_MeV"].sum())
    other = max(0.0, total_ke - classified_total)

    return [
        ("Fragment KE",  float(fragment_ke), COLOR_ION),
        ("Prompt n KE",  float(prompt_n),    COLOR_NEUTRON),
        ("Prompt γ",     float(prompt_g),    COLOR_GAMMA),
        ("β KE",         float(beta_ke),     COLOR_BETA),
        ("β-delayed γ",  float(delayed_g),   "#6a3d9a"),
        ("Anti-νₑ",      float(nu),          COLOR_NU),
        ("Other",        float(other),       COLOR_OTHER),
    ]


def write_partition_bar(
    event_df: pd.DataFrame,
    *,
    event_id: int,
    timestamp: str,
    out_path: Path,
) -> None:
    channels = partition_channels(event_df)
    total = sum(v for _, v, _ in channels)

    fig, ax = plt.subplots(figsize=(11, 6.0))

    left = 0.0
    for name, val, color in channels:
        if val <= 0:
            continue
        ax.barh(0, val, left=left, height=0.5, color=color,
                edgecolor="black", linewidth=0.5, label=name)
        # Above-bar label
        pct = 100 * val / total if total > 0 else 0
        center = left + val / 2
        ax.text(center, 0.32, f"{name}\n{val:.2f} MeV ({pct:.1f}%)",
                ha="center", va="bottom", fontsize=8)
        left += val

    # ENDF reference bar above
    endf_left = 0.0
    for name, val in ENDF_REFERENCE.items():
        color = dict([(n, c) for n, _, c in channels]).get(name, COLOR_OTHER)
        ax.barh(0.9, val, left=endf_left, height=0.25, color=color,
                edgecolor="black", linewidth=0.4, alpha=0.6)
        endf_left += val

    ax.text(ENDF_TOTAL + 1, 0.9,
            f"ENDF/B-VIII.0 ⟨thermal U-235⟩ = {ENDF_TOTAL:.1f} MeV",
            va="center", fontsize=8, style="italic")
    ax.text(total + 1, 0,
            f"this event = {total:.2f} MeV",
            va="center", fontsize=9, weight="bold")

    ax.set_xlim(0, max(total, ENDF_TOTAL) * 1.25)
    ax.set_ylim(-0.6, 1.4)
    ax.set_yticks([0, 0.9])
    ax.set_yticklabels(["this event", "ENDF mean"])
    ax.set_xlabel("Energy [MeV]")
    ax.set_title(
        f"Event {event_id} fission energy partition — page 2 of 3\n"
        f"run {timestamp}  ·  total tracked KE = {total:.2f} MeV  ·  "
        f"delta electrons excluded",
        fontsize=11, loc="left",
    )
    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)
    ax.spines["left"].set_visible(False)
    ax.tick_params(left=False)
    ax.grid(True, axis="x", alpha=0.25)

    fig.tight_layout()
    fig.savefig(out_path, bbox_inches="tight")
    plt.close(fig)


# ---------- page 3: nuclear decay scheme ----------

def walk_chain(
    event_df: pd.DataFrame,
    fragment_track_id: int,
    tree: dict[int, list[int]],
    by_id: dict[int, pd.Series],
) -> list[dict]:
    """Walk a fragment's decay chain. Each list entry describes one ion in the
    chain plus (if it decays) the emissions of its decay step.

    Returns: [
      {
        "ion_name": str, "ion_track_id": int,
        "decays": bool,
        "decay_q_mev": float,
        "emissions": [(particle_name, ke_mev), ...],
        "daughter_track_id": int | None,
      }, ...
    ]
    """
    result = []
    cur = fragment_track_id
    visited = set()
    while cur is not None and cur not in visited:
        visited.add(cur)
        cur_row = by_id[cur]
        cur_name = str(cur_row["particle"])

        # Find RadioactiveDecay children of cur
        kids = tree.get(cur, [])
        decay_children = [
            by_id[k] for k in kids
            if str(by_id[k]["creator_process"]) == "RadioactiveDecay"
        ]

        if not decay_children:
            result.append({
                "ion_name": cur_name,
                "ion_track_id": cur,
                "decays": False,
                "decay_q_mev": 0.0,
                "emissions": [],
                "daughter_track_id": None,
            })
            break

        # Identify daughter ion (single ion among decay children)
        ion_kids = [c for c in decay_children if is_ion(str(c["particle"]))]
        daughter_id = int(ion_kids[0]["track_id"]) if ion_kids else None

        # Emissions = all non-daughter-ion decay children
        emissions = []
        q_total = 0.0
        for c in decay_children:
            cname = str(c["particle"])
            cke = float(c["initial_KE_MeV"])
            q_total += cke
            if daughter_id is not None and int(c["track_id"]) == daughter_id:
                # Daughter recoil: count its KE in Q but don't list as emission
                continue
            emissions.append((cname, cke))

        result.append({
            "ion_name": cur_name,
            "ion_track_id": cur,
            "decays": True,
            "decay_q_mev": q_total,
            "emissions": emissions,
            "daughter_track_id": daughter_id,
        })
        cur = daughter_id

    return result


def draw_decay_scheme(ax, chain: list[dict], title: str) -> None:
    if not chain:
        ax.text(0.5, 0.5, "no chain data", ha="center", va="center",
                transform=ax.transAxes)
        ax.set_title(title, fontsize=10)
        ax.axis("off")
        return

    # Compute cumulative Q at each level (level_y is at y = -cumulative_Q)
    n_levels = len(chain)
    cumulative_q = [0.0]
    for step in chain[:-1]:
        cumulative_q.append(cumulative_q[-1] + step["decay_q_mev"])

    total_q = cumulative_q[-1] + (chain[-1]["decay_q_mev"] if chain[-1]["decays"] else 0)
    if total_q <= 0:
        total_q = 1.0  # avoid degenerate axis

    # x positions for level lines and annotations
    x_line_left, x_line_right = 0.25, 0.55
    x_ion_label = 0.05  # left of line, holding "Xe137"
    x_emission = 0.6   # right of line, holding emissions

    for i, step in enumerate(chain):
        y = -cumulative_q[i]
        # Horizontal level line
        ax.plot([x_line_left, x_line_right], [y, y], color="black", linewidth=1.3)
        # Ion name + cumulative Q to here (left)
        ax.text(x_ion_label, y, step["ion_name"],
                ha="left", va="center", fontsize=11, fontweight="bold")
        ax.text((x_line_left + x_line_right) / 2, y + total_q * 0.012,
                f"cum. Q = {cumulative_q[i]:.2f} MeV",
                ha="center", va="bottom", fontsize=7, color="#555555")

        if step["decays"] and i + 1 < n_levels:
            y_next = -cumulative_q[i + 1]
            # Vertical arrow from this level to next
            ax.annotate(
                "",
                xy=((x_line_left + x_line_right) / 2, y_next),
                xytext=((x_line_left + x_line_right) / 2, y),
                arrowprops=dict(arrowstyle="->", color="#444444", lw=1.0),
            )
            # Emission list to the right of the arrow
            mid_y = (y + y_next) / 2
            lines = [f"Q = {step['decay_q_mev']:.3f} MeV"]
            # Sort emissions: e- first, then γ, then α, then ν̄, then other
            order = {"e-": 0, "e+": 0, "gamma": 1, "alpha": 2, "anti_nu_e": 3}
            sorted_em = sorted(
                step["emissions"],
                key=lambda x: (order.get(x[0], 9), -x[1]),
            )
            for name, ke in sorted_em:
                lines.append(f"{display_name(name)}: {ke:.3f} MeV")
            ax.text(x_emission, mid_y, "\n".join(lines),
                    ha="left", va="center", fontsize=8, family="monospace")
        elif not step["decays"]:
            # Terminal nuclide annotation
            ax.text(x_emission, y, "(stable / no decay\nin sim window)",
                    ha="left", va="center", fontsize=8, style="italic",
                    color="#666666")

    # Set limits
    ax.set_xlim(0, 1)
    ax.set_ylim(-total_q * 1.1, total_q * 0.05)
    ax.set_ylabel("cumulative Q released [MeV]", fontsize=9)
    ax.set_title(title, fontsize=10, loc="left")
    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)
    ax.spines["bottom"].set_visible(False)
    ax.tick_params(bottom=False, labelbottom=False)
    ax.grid(True, axis="y", alpha=0.2)


def write_decay_scheme(
    event_df: pd.DataFrame,
    *,
    event_id: int,
    timestamp: str,
    fragment_a_pdg: int | None,
    fragment_b_pdg: int | None,
    out_path: Path,
) -> None:
    by_id: dict[int, pd.Series] = {
        int(r["track_id"]): r for _, r in event_df.iterrows()
    }
    tree = build_decay_tree(event_df)

    # Find fragment track_ids: nFissionHP children that are ions
    fragments = []
    nfh = event_df[
        (event_df["creator_process"] == "nFissionHP")
        & event_df["particle"].astype(str).map(is_ion)
    ]
    for _, r in nfh.iterrows():
        fragments.append(int(r["track_id"]))

    # Pad to two fragments (some events might genuinely have only one tagged)
    fragments = (fragments + [None, None])[:2]

    chains = []
    for ftid in fragments:
        if ftid is None:
            chains.append([])
        else:
            chains.append(walk_chain(event_df, ftid, tree, by_id))

    fig, axes = plt.subplots(1, 2, figsize=(11, 14))
    titles = []
    for i, (chain, ftid) in enumerate(zip(chains, fragments)):
        if ftid is not None and chain:
            label = f"fragment {chr(ord('A') + i)}: {chain[0]['ion_name']} chain"
        else:
            label = f"fragment {chr(ord('A') + i)}: (no chain)"
        titles.append(label)
        draw_decay_scheme(axes[i], chain, label)

    fig.suptitle(
        f"Event {event_id} decay schemes — page 3 of 3\n"
        f"run {timestamp}  ·  delta electrons excluded",
        fontsize=11, x=0.02, ha="left",
    )
    fig.tight_layout(rect=[0, 0, 1, 0.96])
    fig.savefig(out_path, bbox_inches="tight")
    plt.close(fig)


# ---------- assembly ----------

def merge_pdfs(input_paths: list[Path], output_path: Path) -> None:
    writer = PdfWriter()
    for p in input_paths:
        writer.append(str(p))
    with output_path.open("wb") as f:
        writer.write(f)
    writer.close()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "timestamp",
        help="Run directory name under data/, e.g. 20260430T051304",
    )
    args = parser.parse_args()

    repo_root = find_repo_root(Path(__file__).resolve().parent)
    run_dir = repo_root / "data" / args.timestamp
    truth_csv = run_dir / "truth-record.csv"
    events_csv = run_dir / "events-truth.csv"

    if not run_dir.is_dir():
        print(f"error: run directory not found: {run_dir}", file=sys.stderr)
        return 1
    if not truth_csv.is_file():
        print(f"error: truth-record.csv not found: {truth_csv}", file=sys.stderr)
        return 1
    if not events_csv.is_file():
        print(f"error: events-truth.csv not found: {events_csv}", file=sys.stderr)
        return 1

    out_dir = repo_root / "plots" / f"{args.timestamp}_plots" / "energy-budgets"
    out_dir.mkdir(parents=True, exist_ok=True)

    df = pd.read_csv(truth_csv)
    df_filtered = df[df["creator_process"].isin(NUCLEAR_PROCESSES)]

    events_df = pd.read_csv(events_csv, comment="#")

    n_written = 0
    for _, row in events_df.iterrows():
        event_id = int(row["event_id"])
        event_df = df_filtered[df_filtered["event_id"] == event_id]
        if event_df.empty:
            print(f"warning: event {event_id} has no nuclear-process tracks; skipping",
                  file=sys.stderr)
            continue
        z_a = pdg_to_z(int(row["fragment_A_PDG"])) if pd.notna(row["fragment_A_PDG"]) else None
        z_b = pdg_to_z(int(row["fragment_B_PDG"])) if pd.notna(row["fragment_B_PDG"]) else None
        a_pdg = int(row["fragment_A_PDG"]) if pd.notna(row["fragment_A_PDG"]) else None
        b_pdg = int(row["fragment_B_PDG"]) if pd.notna(row["fragment_B_PDG"]) else None

        out_path = out_dir / f"event_{event_id}_energy_budget.pdf"

        with tempfile.TemporaryDirectory() as td:
            p1 = Path(td) / "page1_sankey.pdf"
            p2 = Path(td) / "page2_partition.pdf"
            p3 = Path(td) / "page3_scheme.pdf"

            write_etotal_sankey(
                event_df,
                event_id=event_id,
                timestamp=args.timestamp,
                fragment_zs=(z_a, z_b),
                out_path=p1,
            )
            write_partition_bar(
                event_df,
                event_id=event_id,
                timestamp=args.timestamp,
                out_path=p2,
            )
            write_decay_scheme(
                event_df,
                event_id=event_id,
                timestamp=args.timestamp,
                fragment_a_pdg=a_pdg,
                fragment_b_pdg=b_pdg,
                out_path=p3,
            )
            merge_pdfs([p1, p2, p3], out_path)

        n_written += 1

    print(f"wrote {n_written} 3-page PDFs to {out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
