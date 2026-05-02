#!/usr/bin/env python3
"""Plot per-particle-category initial-KE histograms from truth-record.csv.

For a run directory data/<timestamp>/, writes PDFs to
plots/<timestamp>_plots/energy-spectra/. One PDF per category, plus an extra
e- spectrum that does not filter out delta electrons.
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

NUCLEAR_PROCESSES = {"primary", "nFissionHP", "RadioactiveDecay"}
ION_NAME_RE = re.compile(r"^[A-Z][a-z]?\d+$")

CATEGORIES = ["neutron", "gamma", "beta", "alpha", "ion", "other"]

# x-axis scale, fixed lower edge for log scales, bin count
BIN_SPEC = {
    "neutron": ("linear", None, 80),
    "gamma":   ("linear", None, 80),
    "beta":    ("log",    1e-3, 60),
    "alpha":   ("linear", None, 50),
    "ion":     ("log",    1e-3, 80),
    "other":   ("linear", None, 50),
}


def categorize(particle: str) -> str:
    if particle == "neutron":
        return "neutron"
    if particle == "gamma":
        return "gamma"
    if particle in ("e-", "e+"):
        return "beta"
    if particle == "alpha":
        return "alpha"
    if ION_NAME_RE.match(particle):
        return "ion"
    return "other"


def find_repo_root(start: Path) -> Path:
    for p in [start, *start.parents]:
        if (p / "nuclear-fission.cc").is_file():
            return p
    raise RuntimeError(
        f"Could not find repo root (no nuclear-fission.cc found above {start})"
    )


def read_thermal_neutron_count(events_truth_csv: Path) -> int | None:
    if not events_truth_csv.is_file():
        return None
    with events_truth_csv.open() as f:
        first = f.readline().strip()
    m = re.match(r"#\s*n_thermal_neutrons\s*=\s*(\d+)", first)
    return int(m.group(1)) if m else None


def make_bins(
    values: np.ndarray, xscale: str, log_lo: float | None, n_bins: int
) -> np.ndarray:
    if values.size == 0:
        return np.linspace(0.0, 1.0, n_bins + 1)
    vmax = float(values.max())
    if xscale == "log":
        positive = values[values > 0]
        if positive.size == 0:
            return np.linspace(0.0, 1.0, n_bins + 1)
        lo = max(log_lo or 1e-6, float(positive.min()) * 0.9)
        hi = max(vmax * 1.1, lo * 10)
        return np.logspace(np.log10(lo), np.log10(hi), n_bins + 1)
    hi = vmax * 1.05 if vmax > 0 else 1.0
    return np.linspace(0.0, hi, n_bins + 1)


def plot_spectrum(
    energies: np.ndarray,
    *,
    title: str,
    xscale: str,
    log_lo: float | None,
    n_bins: int,
    n_events: int,
    out_path: Path,
    info_lines: list[str],
) -> None:
    fig, ax = plt.subplots(figsize=(7.5, 5.0))

    if energies.size == 0 or n_events <= 0:
        ax.text(
            0.5, 0.5, "no entries",
            ha="center", va="center", transform=ax.transAxes, fontsize=14,
        )
    else:
        plot_values = energies[energies > 0] if xscale == "log" else energies
        bins = make_bins(plot_values, xscale, log_lo, n_bins)
        weights = np.full(len(plot_values), 1.0 / n_events)
        ax.hist(plot_values, bins=bins, weights=weights, histtype="step", linewidth=1.4)

        if xscale == "log":
            ax.set_xscale("log")
        ax.set_yscale("log")

    ax.set_xlabel("Initial kinetic energy [MeV]")
    ax.set_ylabel("Tracks per fission per bin")
    ax.set_title(title)
    ax.grid(True, which="both", alpha=0.25)

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
    truth_csv = run_dir / "truth-record.csv"
    events_csv = run_dir / "events-truth.csv"

    if not run_dir.is_dir():
        print(f"error: run directory not found: {run_dir}", file=sys.stderr)
        return 1
    if not truth_csv.is_file():
        print(f"error: truth-record.csv not found: {truth_csv}", file=sys.stderr)
        return 1
    if not events_csv.is_file():
        print(f"error: events-truth.csv not found: {events_csv} (needed for /N_events normalization)", file=sys.stderr)
        return 1

    out_dir = repo_root / "plots" / f"{args.timestamp}_plots" / "energy-spectra"
    out_dir.mkdir(parents=True, exist_ok=True)

    df = pd.read_csv(truth_csv)
    df["category"] = df["particle"].map(categorize)

    df_filtered = df[df["creator_process"].isin(NUCLEAR_PROCESSES)]

    n_thermal = read_thermal_neutron_count(events_csv)
    events_df = pd.read_csv(events_csv, comment="#")
    n_events = len(events_df)
    if n_events == 0:
        print(f"error: events-truth.csv has zero events: {events_csv}", file=sys.stderr)
        return 1

    base_info = [
        f"run        {args.timestamp}",
        f"thermal n  {n_thermal if n_thermal is not None else '?'}",
        f"events     {n_events}",
        f"normalized /{n_events} events",
    ]

    for cat in CATEGORIES:
        sub = df_filtered[df_filtered["category"] == cat]
        xscale, log_lo, n_bins = BIN_SPEC[cat]
        info = base_info + [f"tracks     {len(sub)}", "filter     primary+nFissionHP+RadDecay"]
        plot_spectrum(
            sub["initial_KE_MeV"].to_numpy(),
            title=f"{cat} initial KE — run {args.timestamp}",
            xscale=xscale, log_lo=log_lo, n_bins=n_bins,
            n_events=n_events,
            out_path=out_dir / f"energy_spectrum_{cat}.pdf",
            info_lines=info,
        )

    extra = df[df["particle"].isin({"e-", "e+"})]
    info = base_info + [f"tracks     {len(extra)}", "filter     none (incl. delta e-)"]
    plot_spectrum(
        extra["initial_KE_MeV"].to_numpy(),
        title=f"beta initial KE (with deltas) — run {args.timestamp}",
        xscale="log", log_lo=1e-4, n_bins=80,
        n_events=n_events,
        out_path=out_dir / "energy_spectrum_beta_with_deltas.pdf",
        info_lines=info,
    )

    print(f"wrote 7 PDFs to {out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
