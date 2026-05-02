#!/usr/bin/env python3
"""Per-event Sankey diagrams of the fission-event energy budget.

For a run directory data/<timestamp>/, writes one PDF per fission event to
plots/<timestamp>_plots/energy-budgets/, depicting how the event's truth-level
initial kinetic energy is distributed across particle types in the prompt
emission and the radioactive-decay chain. Delta electrons (and other EM
transport secondaries) are excluded.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

import pandas as pd
import plotly.graph_objects as go

NUCLEAR_PROCESSES = {"primary", "nFissionHP", "RadioactiveDecay"}
PROMPT_PROCESSES = {"primary", "nFissionHP"}
CHAIN_PROCESSES = {"RadioactiveDecay"}
ION_NAME_RE = re.compile(r"^[A-Z][a-z]?\d+$")

# Bucket definitions: (label, predicate on row dict, color)
# Each row has fields: particle, creator_process. Order here = order of
# leaf nodes in the Sankey (prompt buckets first, then chain).
PROMPT_BUCKETS = [
    ("Fragments",       lambda p, c: c == "nFissionHP" and ION_NAME_RE.match(p), "#b15928"),
    ("Prompt neutrons", lambda p, c: c in PROMPT_PROCESSES and p == "neutron",   "#1f78b4"),
    ("Prompt gammas",   lambda p, c: c == "nFissionHP" and p == "gamma",         "#33a02c"),
]
CHAIN_BUCKETS = [
    ("Decay betas",      lambda p, c: c == "RadioactiveDecay" and p in ("e-", "e+"),  "#e31a1c"),
    ("Decay gammas",     lambda p, c: c == "RadioactiveDecay" and p == "gamma",       "#6a3d9a"),
    ("Decay alphas",     lambda p, c: c == "RadioactiveDecay" and p == "alpha",       "#ff7f00"),
    ("Anti-neutrinos",   lambda p, c: c == "RadioactiveDecay" and p == "anti_nu_e",   "#a6cee3"),
    ("Daughter recoils", lambda p, c: c == "RadioactiveDecay" and bool(ION_NAME_RE.match(p)), "#cab2d6"),
    ("Decay other",      lambda p, c: c == "RadioactiveDecay" and not (
        p in ("e-", "e+", "gamma", "alpha", "anti_nu_e") or ION_NAME_RE.match(p)
    ), "#999999"),
]


def find_repo_root(start: Path) -> Path:
    for p in [start, *start.parents]:
        if (p / "nuclear-fission.cc").is_file():
            return p
    raise RuntimeError(
        f"Could not find repo root (no nuclear-fission.cc found above {start})"
    )


def pdg_to_z(pdg: int) -> int | None:
    """Return Z for a Geant4 ion PDG code (1000ZZZAAA0), or None for non-ions."""
    if pdg < 1_000_000_000:
        return None
    return (pdg // 10_000) % 1000


def aggregate_buckets(event_df: pd.DataFrame, buckets: list) -> list[dict]:
    """For each bucket, sum KE and count tracks. Returns dicts with name/sum_ke/count/color."""
    import numpy as np

    out = []
    particles = event_df["particle"].to_numpy()
    procs = event_df["creator_process"].to_numpy()
    kes = event_df["initial_KE_MeV"].to_numpy()
    for label, pred, color in buckets:
        mask = np.array([bool(pred(p, c)) for p, c in zip(particles, procs)], dtype=bool)
        sub = kes[mask]
        out.append({
            "label": label,
            "sum_ke": float(sub.sum()),
            "count": int(mask.sum()),
            "color": color,
        })
    return out


def build_sankey_for_event(
    event_df: pd.DataFrame,
    *,
    event_id: int,
    timestamp: str,
    fragment_zs: tuple[int | None, int | None],
    out_path: Path,
) -> None:
    prompt = aggregate_buckets(event_df, PROMPT_BUCKETS)
    chain = aggregate_buckets(event_df, CHAIN_BUCKETS)

    prompt_total = sum(b["sum_ke"] for b in prompt)
    chain_total = sum(b["sum_ke"] for b in chain)
    total = prompt_total + chain_total

    # Layout: 3 columns of nodes.
    # Node 0: Total. Nodes 1,2: Prompt, Decay chain. Then leaf nodes.
    nodes_label = []
    nodes_color = []
    sources, targets, values, link_labels, link_colors = [], [], [], [], []

    def fmt(label: str, ke: float, count: int | None) -> str:
        if count is None:
            return f"{label}<br>{ke:.2f} MeV"
        return f"{label}<br>{ke:.2f} MeV<br>n = {count}"

    # Node 0: Total
    nodes_label.append(fmt("Total tracked KE", total, None))
    nodes_color.append("#222222")

    # Node 1: Prompt
    nodes_label.append(fmt("Prompt", prompt_total, None))
    nodes_color.append("#08519c")
    # Node 2: Decay chain
    nodes_label.append(fmt("Decay chain", chain_total, None))
    nodes_color.append("#54278f")

    # Trunk links
    if prompt_total > 0:
        sources.append(0)
        targets.append(1)
        values.append(prompt_total)
        link_labels.append(f"{prompt_total:.2f} MeV")
        link_colors.append("rgba(8,81,156,0.35)")
    if chain_total > 0:
        sources.append(0)
        targets.append(2)
        values.append(chain_total)
        link_labels.append(f"{chain_total:.2f} MeV")
        link_colors.append("rgba(84,39,143,0.35)")

    def add_leaf(parent_idx: int, bucket: dict) -> None:
        if bucket["sum_ke"] <= 0:
            return
        leaf_idx = len(nodes_label)
        nodes_label.append(fmt(bucket["label"], bucket["sum_ke"], bucket["count"]))
        nodes_color.append(bucket["color"])
        sources.append(parent_idx)
        targets.append(leaf_idx)
        values.append(bucket["sum_ke"])
        link_labels.append(f"{bucket['sum_ke']:.2f} MeV (n={bucket['count']})")
        # rgba with alpha; convert hex → rgba string
        h = bucket["color"].lstrip("#")
        r, g, b = int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)
        link_colors.append(f"rgba({r},{g},{b},0.5)")

    for b in prompt:
        add_leaf(1, b)
    for b in chain:
        add_leaf(2, b)

    z_a, z_b = fragment_zs
    z_str = ""
    if z_a is not None and z_b is not None:
        z_str = f"  ·  fragments Z={z_a}+Z={z_b}"
    title = (
        f"Event {event_id} energy budget — run {timestamp}{z_str}<br>"
        f"<sub>total tracked KE: {total:.2f} MeV  ·  delta electrons excluded</sub>"
    )

    fig = go.Figure(data=[go.Sankey(
        arrangement="snap",
        node=dict(
            pad=18,
            thickness=18,
            line=dict(color="black", width=0.5),
            label=nodes_label,
            color=nodes_color,
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
        title=dict(text=title, x=0.02, xanchor="left", font=dict(size=13)),
        font=dict(size=11),
        margin=dict(l=20, r=20, t=80, b=20),
        width=1100,
        height=650,
    )
    fig.write_image(out_path, format="pdf", engine="kaleido")


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

        out_path = out_dir / f"event_{event_id}_energy_budget.pdf"
        build_sankey_for_event(
            event_df,
            event_id=event_id,
            timestamp=args.timestamp,
            fragment_zs=(z_a, z_b),
            out_path=out_path,
        )
        n_written += 1

    print(f"wrote {n_written} PDFs to {out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
