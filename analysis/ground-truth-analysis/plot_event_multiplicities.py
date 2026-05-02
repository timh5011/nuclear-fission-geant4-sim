#!/usr/bin/env python3
"""Plot per-event particle multiplicities from events-truth.csv.

For a run directory data/<timestamp>/, writes 8 PDFs to
plots/<timestamp>_plots/multiplicities/, one per multiplicity column. Each
histogram bin is one integer.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

# (column, display name, file slug)
COLUMNS = [
    ("n_prompt_neutrons", "Prompt neutrons",                 "prompt_neutron"),
    ("n_chain_neutrons",  "Chain neutrons (prompt + delayed)", "chain_neutron"),
    ("n_prompt_gammas",   "Prompt gammas",                    "prompt_gamma"),
    ("n_chain_gammas",    "Chain gammas",                     "chain_gamma"),
    ("n_chain_betas",     "Chain betas",                      "chain_beta"),
    ("n_chain_alphas",    "Chain alphas",                     "chain_alpha"),
    ("n_chain_ions",      "Chain ions (>=2 fragments)",       "chain_ion"),
    ("n_chain_other",     "Chain other (anti-nu_e etc.)",     "chain_other"),
]


def find_repo_root(start: Path) -> Path:
    for p in [start, *start.parents]:
        if (p / "nuclear-fission.cc").is_file():
            return p
    raise RuntimeError(
        f"Could not find repo root (no nuclear-fission.cc found above {start})"
    )


def read_thermal_neutron_count(events_csv: Path) -> int | None:
    with events_csv.open() as f:
        first = f.readline().strip()
    m = re.match(r"#\s*n_thermal_neutrons\s*=\s*(\d+)", first)
    return int(m.group(1)) if m else None


def plot_multiplicity(
    values: np.ndarray,
    *,
    title: str,
    out_path: Path,
    info_lines: list[str],
) -> None:
    fig, ax = plt.subplots(figsize=(7.5, 5.0))

    if values.size == 0:
        ax.text(
            0.5, 0.5, "no events",
            ha="center", va="center", transform=ax.transAxes, fontsize=14,
        )
    else:
        max_val = int(values.max()) if values.size else 0
        bins = np.arange(-0.5, max_val + 1.5, 1.0)
        ax.hist(values, bins=bins, histtype="stepfilled", edgecolor="black",
                linewidth=1.0, alpha=0.6)
        ax.set_xticks(np.arange(0, max_val + 1))

    ax.set_xlabel("Multiplicity per event")
    ax.set_ylabel("Events")
    ax.set_title(title)
    ax.grid(True, axis="y", alpha=0.25)

    if info_lines:
        ax.text(
            0.98, 0.98, "\n".join(info_lines),
            transform=ax.transAxes, ha="right", va="top",
            fontsize=8, family="monospace",
            bbox=dict(boxstyle="round,pad=0.4", fc="white", ec="0.7", alpha=0.85),
        )

    fig.tight_layout()
    fig.savefig(out_path, bbox_inches="tight")
    plt.close(fig)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "timestamp",
        help="Run directory name under data/, e.g. 20260430T051304",
    )
    args = parser.parse_args()

    repo_root = find_repo_root(Path(__file__).resolve().parent)
    run_dir = repo_root / "data" / args.timestamp
    events_csv = run_dir / "events-truth.csv"

    if not run_dir.is_dir():
        print(f"error: run directory not found: {run_dir}", file=sys.stderr)
        return 1
    if not events_csv.is_file():
        print(f"error: events-truth.csv not found: {events_csv}", file=sys.stderr)
        return 1

    out_dir = repo_root / "plots" / f"{args.timestamp}_plots" / "multiplicities"
    out_dir.mkdir(parents=True, exist_ok=True)

    n_thermal = read_thermal_neutron_count(events_csv)
    df = pd.read_csv(events_csv, comment="#")

    base_info = [
        f"run        {args.timestamp}",
        f"thermal n  {n_thermal if n_thermal is not None else '?'}",
        f"events     {len(df)}",
    ]

    sanity_means = {
        "n_prompt_neutrons": "Terrell ~2.42",
        "n_prompt_gammas":   "expected ~7-8",
    }

    for col, display, slug in COLUMNS:
        values = df[col].dropna().to_numpy()
        if values.size:
            mean = float(values.mean())
            std = float(values.std(ddof=0))
            stat_line = f"mean+-std  {mean:.2f} +- {std:.2f}"
        else:
            stat_line = "mean+-std  n/a"
        info = base_info + [stat_line]
        if col in sanity_means:
            info.append(f"reference  {sanity_means[col]}")

        plot_multiplicity(
            values,
            title=f"{display} per event - run {args.timestamp}",
            out_path=out_dir / f"multiplicity_{slug}.pdf",
            info_lines=info,
        )

    print(f"wrote {len(COLUMNS)} PDFs to {out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
