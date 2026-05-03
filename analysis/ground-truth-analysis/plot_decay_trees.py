#!/usr/bin/env python3
"""Per-event nuclear decay tree PDFs (graphviz LR layout).

Companion view to ``plot_decay_schemes.py``: same chain-shaping pipeline,
different visual style. Each chain entry becomes one record-shaped node
carrying nuclide / cumulative Q / decay-step summary; nodes are connected
left-to-right by bare arrows. ``dot`` lays out the graph such that no two
node bounding boxes can overlap, by construction — which is the design
intent: the tree view scales cleanly to dense low-Q chain tails where the
fixed-coordinate NNDC scheme runs out of room.

Output: plots/<ts>_plots/decay-trees/event_<id>_decay_tree.pdf, one PDF per
fission event.

Reuses chain-shaping (walk_chain → filter_chain_emissions → coalesce_isomers)
and label-formatting helpers from ``plot_decay_schemes`` so the two diagrams
stay semantically consistent. Requires the ``dot`` binary on $PATH (Homebrew
``brew install graphviz`` or Anaconda's ``graphviz`` package).
"""
from __future__ import annotations

import argparse
import shutil
import sys
from html import escape
from pathlib import Path

import pandas as pd
import graphviz

# Reuse everything chain-shaping from the scheme module so the two scripts
# share semantics by construction (no duplicated chain logic).
from plot_decay_schemes import (
    NUCLEAR_PROCESSES,
    EMISSION_KE_FLOOR_MEV,
    MAX_DRIVER_LINES,
    find_repo_root,
    parse_ion,
    is_ion,
    build_decay_tree,
    walk_chain,
    filter_chain_emissions,
    coalesce_isomers,
    classify_decay,
)

# ── HTML label palette ────────────────────────────────────────────────────────

# Border color (full saturation) and bottom-row fill (light) keyed by the
# nuclide's outgoing decay kind. Same hex codes the NNDC arrows use.
KIND_BORDER: dict[str, str] = {
    "betam":      "#A93226",   # red!75!black
    "betap":      "#1F618D",   # blue!75!black
    "alpha":      "#B9770E",   # orange!80!black
    "gammaarrow": "#196F3D",   # green!50!black
    "terminal":   "#7F8C8D",   # gray
}
KIND_FILL: dict[str, str] = {
    "betam":      "#F5B7B1",   # light coral
    "betap":      "#AED6F1",   # light blue
    "alpha":      "#F5CBA7",   # light orange
    "gammaarrow": "#A9DFBF",   # light green
    "terminal":   "#D5DBDB",   # light grey
}
HEADER_FILL = "#FFF8DC"        # light yellow — nuclide identity strip
ARRIVAL_FILL = "#FFFFFF"       # white — cumulative-Q strip


# ── plain-UTF-8 label helpers (graphviz HTML doesn't render TeX) ──────────────

def _kind_label_plain(kind: str) -> str:
    return {
        "betam": "β⁻",
        "betap": "β⁺",
        "alpha": "α",
        "gammaarrow": "γ",
    }[kind]


def _format_driver_lines_plain(drivers: list[tuple[str, float]]) -> list[str]:
    """Plain-UTF-8 mirror of plot_decay_schemes._format_driver_lines.

    Same combined-β⁻+ν̄_e rule, same MAX_DRIVER_LINES cap; just produces
    HTML-safe strings instead of TeX fragments.
    """
    by_kind: dict[str, list[float]] = {}
    for p, ke in drivers:
        by_kind.setdefault(p, []).append(ke)

    lines: list[str] = []

    if "e-" in by_kind and "anti_nu_e" in by_kind:
        em_ke = max(by_kind["e-"])
        nu_ke = max(by_kind["anti_nu_e"])
        lines.append(f"β⁻+ν̄_e: {em_ke:.3f} + {nu_ke:.3f} MeV")
        extras = sorted(by_kind["e-"], reverse=True)[1:]
        for ke in extras[:1]:
            lines.append(f"β⁻: {ke:.3f}")
    elif "e+" in by_kind and "nu_e" in by_kind:
        em_ke = max(by_kind["e+"])
        nu_ke = max(by_kind["nu_e"])
        lines.append(f"β⁺+ν_e: {em_ke:.3f} + {nu_ke:.3f} MeV")
    else:
        sorted_drivers = sorted(drivers, key=lambda x: -x[1])
        for p, ke in sorted_drivers:
            if p == "e-":
                lines.append(f"β⁻: {ke:.3f}")
            elif p == "e+":
                lines.append(f"β⁺: {ke:.3f}")
            elif p == "alpha":
                lines.append(f"α: {ke:.3f}")
            elif p == "anti_nu_e":
                lines.append(f"ν̄_e: {ke:.3f}")
            elif p == "nu_e":
                lines.append(f"ν_e: {ke:.3f}")
            else:
                lines.append(f"{p}: {ke:.3f}")

    return lines[:MAX_DRIVER_LINES]


# ── nuclide-symbol prefix (^{A}Sym, plain UTF-8 superscripts) ─────────────────

_SUP_DIGITS = str.maketrans("0123456789", "⁰¹²³⁴⁵⁶⁷⁸⁹")


def _nuclide_label_plain(ion_name: str) -> str:
    """'Ba150' → '¹⁵⁰Ba'. Falls back to the raw name if unparsable."""
    p = parse_ion(ion_name)
    if not p:
        return ion_name
    sym, a = p
    return f"{str(a).translate(_SUP_DIGITS)}{sym}"


# ── HTML node label assembly ──────────────────────────────────────────────────

def _html_row(
    text: str,
    *,
    bg: str,
    bold: bool = False,
    pointsize: int = 10,
    align: str = "CENTER",
    colspan: int = 1,
) -> str:
    """One TR of the HTML record table."""
    inner = escape(text, quote=False)
    if bold:
        inner = f"<B>{inner}</B>"
    span_attr = f' COLSPAN="{colspan}"' if colspan != 1 else ""
    return (
        f'<TR><TD ALIGN="{align}" BGCOLOR="{bg}"'
        f' CELLPADDING="4"{span_attr}>'
        f'<FONT POINT-SIZE="{pointsize}">{inner}</FONT>'
        f'</TD></TR>'
    )


def _build_node_label(
    *,
    ion_name: str,
    creator_process: str,
    cumulative_q_mev: float,
    decay_kind: str,                         # 'betam'|'betap'|'alpha'|'gammaarrow'|'terminal'
    q_total_mev: float,
    drivers: list[tuple[str, float]],
    n_gammas: int,
    sum_gamma_mev: float,
) -> str:
    """Build the graphviz HTML label for one nuclide record node."""
    rows: list[str] = []

    # Header — nuclide identity
    rows.append(_html_row(_nuclide_label_plain(ion_name),
                          bg=HEADER_FILL, bold=True, pointsize=14))

    # Arrival info — cum Q, creator process
    rows.append(_html_row(f"cum Q = {cumulative_q_mev:.3f} MeV",
                          bg=ARRIVAL_FILL, pointsize=10))
    rows.append(_html_row(f"created via: {creator_process}",
                          bg=ARRIVAL_FILL, pointsize=9))

    # Decay summary (or terminal indicator)
    bottom_bg = KIND_FILL[decay_kind]
    if decay_kind == "terminal":
        rows.append(_html_row("(stable / no decay",
                              bg=bottom_bg, pointsize=10))
        rows.append(_html_row("in sim window)",
                              bg=bottom_bg, pointsize=10))
    elif decay_kind == "gammaarrow":
        # Pure-γ IT step: no β/α drivers; just show γ count + total
        if n_gammas == 1:
            rows.append(_html_row(f"γ-only IT, Q = {q_total_mev:.3f} MeV",
                                  bg=bottom_bg, bold=True, pointsize=10))
            rows.append(_html_row(f"γ: {sum_gamma_mev:.3f} MeV",
                                  bg=bottom_bg, pointsize=10))
        else:
            rows.append(_html_row(f"γ-only IT, Q = {q_total_mev:.3f} MeV",
                                  bg=bottom_bg, bold=True, pointsize=10))
            rows.append(_html_row(f"γ × {n_gammas}: {sum_gamma_mev:.3f} MeV",
                                  bg=bottom_bg, pointsize=10))
    else:
        # β / α step
        rows.append(_html_row(
            f"{_kind_label_plain(decay_kind)}, Q = {q_total_mev:.3f} MeV",
            bg=bottom_bg, bold=True, pointsize=10,
        ))
        for line in _format_driver_lines_plain(drivers):
            rows.append(_html_row(line, bg=bottom_bg, pointsize=9))
        if n_gammas >= 1:
            if n_gammas == 1:
                g_text = f"γ: {sum_gamma_mev:.3f} MeV"
            else:
                g_text = f"γ × {n_gammas}: {sum_gamma_mev:.3f} MeV"
            rows.append(_html_row(g_text, bg=bottom_bg, pointsize=9))

    body = "".join(rows)
    return (
        f'<<TABLE BORDER="0" CELLBORDER="0" CELLSPACING="0">{body}</TABLE>>'
    )


# ── per-event tree construction ───────────────────────────────────────────────

def _emit_chain(
    sg: graphviz.Digraph,
    chain: list[dict],
    *,
    chain_letter: str,
) -> None:
    """Emit one fragment chain into a subgraph as a left-to-right path."""
    if not chain:
        return

    cumulative_q = 0.0
    prev_node_id: str | None = None

    for step_idx, step in enumerate(chain):
        node_id = f"frag{chain_letter}_step{step_idx}"

        # Decide kind
        if not step["decays"]:
            decay_kind = "terminal"
        else:
            decay_kind = classify_decay(step["emissions"])

        # Drivers / gammas summary
        gammas = [(ke, p) for p, ke in step["emissions"] if p == "gamma"]
        drivers = [(p, ke) for p, ke in step["emissions"] if p != "gamma"]
        sum_gamma = sum(ke for ke, _ in gammas)
        q_total = step["decay_q_mev"]

        # creator process: first chain entry is nFissionHP (the fragment
        # itself is the nFissionHP child); subsequent entries arrived via
        # RadioactiveDecay. We can read it from the truth-record tree but
        # the chain dict doesn't carry it; encode the convention here.
        creator = "nFissionHP" if step_idx == 0 else "RadioactiveDecay"

        label = _build_node_label(
            ion_name=step["ion_name"],
            creator_process=creator,
            cumulative_q_mev=cumulative_q,
            decay_kind=decay_kind,
            q_total_mev=q_total,
            drivers=drivers,
            n_gammas=len(gammas),
            sum_gamma_mev=sum_gamma,
        )
        sg.node(
            node_id,
            label=label,
            shape="plain",       # HTML-table provides the visual border
            color=KIND_BORDER[decay_kind],
            penwidth="2",
        )

        if prev_node_id is not None:
            sg.edge(prev_node_id, node_id, color="black", penwidth="1")

        prev_node_id = node_id
        cumulative_q += q_total


def render_event_tree(
    *,
    chains: list[list[dict]],
    fragment_names: list[str],
    event_id: int,
    timestamp: str,
    out_pdf: Path,
) -> None:
    """Build and render one event's decay-tree PDF."""
    title = f"Event {event_id} — nuclear decay tree"
    frag_str = "  •  ".join(
        f"fragment {chr(ord('A') + k)}: {n}"
        for k, n in enumerate(fragment_names[:2])
    )
    subtitle = (
        f"run {timestamp}"
        + (f"  •  {frag_str}" if frag_str else "")
        + "  •  δ-electrons + sub-5keV emissions filtered"
        + "  •  IT cascades coalesced"
    )

    g = graphviz.Digraph(
        format="pdf",
        graph_attr={
            "rankdir": "LR",
            "splines": "spline",
            "nodesep": "0.4",
            "ranksep": "0.7",
            "fontname": "Helvetica",
            "label": f"{title}\n{subtitle}",
            "labelloc": "t",
            "labeljust": "l",
            "fontsize": "12",
        },
        node_attr={
            "shape": "plain",
            "fontname": "Helvetica",
        },
        edge_attr={
            "fontname": "Helvetica",
            "arrowhead": "vee",
            "arrowsize": "0.8",
        },
    )

    for i, chain in enumerate(chains):
        if not chain:
            continue
        cluster_name = f"cluster_fragment_{chr(ord('A') + i)}"
        with g.subgraph(name=cluster_name) as sg:
            label_name = (
                fragment_names[i] if i < len(fragment_names) else f"chain {i}"
            )
            sg.attr(
                label=f"fragment {chr(ord('A') + i)}: "
                      f"{_nuclide_label_plain(label_name)}",
                style="dashed",
                color="gray70",
                fontsize="11",
                fontname="Helvetica",
                labelloc="t",
                margin="12",
            )
            _emit_chain(sg, chain, chain_letter=chr(ord('A') + i))

    pdf_bytes = g.pipe(format="pdf")
    out_pdf.parent.mkdir(parents=True, exist_ok=True)
    out_pdf.write_bytes(pdf_bytes)


# ── main ──────────────────────────────────────────────────────────────────────

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "timestamp",
        help="Run directory name under data/, e.g. 20260430T052100",
    )
    args = parser.parse_args()

    if not shutil.which("dot"):
        print(
            "error: graphviz `dot` binary not found on $PATH. "
            "Install via Homebrew (`brew install graphviz`) or pip "
            "(`pip install graphviz` only installs the Python bindings; "
            "you also need the system binary).",
            file=sys.stderr,
        )
        return 1

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

    out_dir = repo_root / "plots" / f"{args.timestamp}_plots" / "decay-trees"
    out_dir.mkdir(parents=True, exist_ok=True)

    df = pd.read_csv(truth_csv)
    df_nuclear = df[df["creator_process"].isin(NUCLEAR_PROCESSES)]
    events_df = pd.read_csv(events_csv, comment="#")

    n_written = n_failed = 0
    for _, row in events_df.iterrows():
        event_id = int(row["event_id"])
        event_df = df_nuclear[df_nuclear["event_id"] == event_id]
        if event_df.empty:
            print(
                f"warning: event {event_id}: no nuclear-process tracks; "
                f"skipping",
                file=sys.stderr,
            )
            continue

        by_id = {int(r["track_id"]): r for _, r in event_df.iterrows()}
        tree = build_decay_tree(event_df)

        frag_rows = event_df[
            (event_df["creator_process"] == "nFissionHP")
            & event_df["particle"].astype(str).map(is_ion)
        ]
        fragment_tids = [int(r["track_id"]) for _, r in frag_rows.iterrows()][:2]

        chains: list[list[dict]] = []
        for tid in fragment_tids:
            chain = walk_chain(event_df, tid, tree, by_id)
            chain = filter_chain_emissions(chain)
            chain = coalesce_isomers(chain)
            chains.append(chain)
        fragment_names = [c[0]["ion_name"] for c in chains if c]

        if not any(chains):
            print(
                f"warning: event {event_id}: both chains empty after filter; "
                f"skipping",
                file=sys.stderr,
            )
            continue

        out_pdf = out_dir / f"event_{event_id}_decay_tree.pdf"
        try:
            render_event_tree(
                chains=chains,
                fragment_names=fragment_names,
                event_id=event_id,
                timestamp=args.timestamp,
                out_pdf=out_pdf,
            )
            n_written += 1
        except graphviz.backend.execute.ExecutableNotFound as e:
            print(f"error: event {event_id}: {e}", file=sys.stderr)
            n_failed += 1
        except Exception as e:
            print(f"error: event {event_id}: {e}", file=sys.stderr)
            n_failed += 1

    status = f"wrote {n_written} decay-tree PDFs to {out_dir}"
    if n_failed:
        status += f" ({n_failed} failed)"
    print(status)
    return 0 if n_failed == 0 else 2


if __name__ == "__main__":
    sys.exit(main())
