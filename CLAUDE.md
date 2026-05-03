# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Run

Geant4 v11.4.0 must be on the environment before configuring or running:

```bash
source /path/to/geant4-v11.4.0-install/bin/geant4.sh
```

This export must be in the same shell as `cmake` / `make` / the executable — re-source it for any new terminal. The README notes this explicitly because forgetting it is the most common breakage.

Configure & build (out-of-tree, in `build/`):

```bash
cd build
cmake ..
make -j
./nuclear_fission         # opens the OGL UI session
```

`CMakeLists.txt` globs `src/*.cc` and `include/*.hh`, so adding a new file just means dropping it in those directories — no CMake edit needed. `.mac` files in `src/macros/` are `configure_file`-copied into `build/` at cmake time, so re-run cmake (not just make) after editing a macro.

## Architecture

Standard Geant4 user-application layout. `nuclear-fission.cc` wires four user-initialization classes into `G4RunManager`, sets two pre-`Initialize()` flags, and dispatches to interactive (`./nuclear_fission`) or headless (`./nuclear_fission run.mac`) mode based on `argc`.

The four user classes:

- **`MyDetectorConstruction`** (`src/DetectorConstruction.cc`) — Builds materials in `DefineMaterials()` (called from the constructor, *not* `Construct()`) so material pointers are members. Geometry is a 2 m × 2 m × 2 m air world containing a 20 mm × 0.5 µm pure ²³⁵U foil at the origin, a 48-cell EJ-309 organic scintillator array (80 mm dia × 50 mm length cylinders + 1 mm Al housings, r = 500 mm forward hemisphere; 6 polar rings × 8 azimuthal positions on θ ∈ [20°, 130°], with a Fibonacci-spiral layout retained as a commented swap-in in `BuildEJ309Array`), and 2 LaBr₃(Ce) crystals (120 mm dia × 38 mm length, r = 300 mm at θ = 135°). Pure U-235 is built by hand from a `G4Isotope`/`G4Element` because NIST only provides natural uranium. Both scintillators carry full `G4MaterialPropertiesTable`s with per-particle scintillation yields (PSD-ready) and share a `G4LogicalSkinSurface` Tyvek/PTFE-equivalent diffuse reflective wrap (`unified` model, `dielectric_dielectric` type, `groundfrontpainted` finish, R = 0.98). `ConstructSDandField()` attaches one `ScintillatorSD` per scintillator material — `EJ309SD` (copy-no depth 1, parent housing carries the copy) and `LaBr3SD` (depth 0).
- **`MyPhysicsList`** (`include/Physics.hh`, header-only) — Modular physics list. The header has long block comments documenting *why* each module is registered; preserve those when editing. Hadronic stack is `QGSP_BIC_HP` + `HadronElasticPhysicsHP` (G4NDL/ENDF data for n + ²³⁵U below 20 MeV). EM is `G4EmStandardPhysics_option4`. `G4OpticalPhysics`, `G4RadioactiveDecayPhysics`, and the ion physics modules are all registered. Scintillation, optical transport, and reflective-wrap boundary processes are all live; only optical-photon scoring is deferred.
- **`MyActionInitialization`** (`src/Action.cc`) — Constructs and registers `MyRunAction`, `MyPrimaryGenerator`, `MySteppingAction`, `MyEventAction` (in that order — RunAction first so EventAction can take a pointer to it for writer access; SteppingAction before EventAction so the latter can hand `EventRecord*` to the fission watcher at BoEvent).
- **`MyPrimaryGenerator`** (`src/Generator.cc`) — Single thermal neutron (0.025 eV) at `(0, 0, -100 mm)` aimed `+ẑ` into the foil. No spread / sampling yet.

**Two critical pre-`Initialize()` flags** in `nuclear-fission.cc`:

1. `G4ParticleHPManager::GetInstance()->SetProduceFissionFragments(true)` — without this, the HP package deposits fission energy locally instead of producing explicit fission-fragment ion tracks. Confirmation: `Fission fragment production is now activated in HP package for Z = 92, A = 235` in the run log.
2. `G4OpticalParameters::Instance()->SetScintByParticleType(true)` — required for per-particle scintillation yields (the lever PSD pulls on). In Geant4 v11 this flag moved from `G4OpticalPhysics` to `G4OpticalParameters`. With the flag on, `G4Scintillation::PostStepDoIt` throws fatal `Scint01` if any particle deposits energy in a scintillator that lacks a per-particle yield curve — which is why `DefineMaterials()` populates electron / proton / ion / alpha / deuteron / triton curves on **both** scintillators.

**Output.** Each run writes one `data/<UTC-YYYYMMDDTHHMMSS>/` directory at the repo root containing three CSVs:
- `hits.csv` — one row per non-optical-photon track entry into a sensitive scintillator volume (with nonzero edep). Detector-stage observable. Columns: `event_id, detector_id, track_id, particle, creator_process, entry_time_ns, energy_dep_MeV`.
- `truth-record.csv` — one row per track produced anywhere in the event, with optical photons and delta electrons (e- whose creator process ends in `Ioni`) filtered out at source in `MyTrackingAction`. Source-stage ground truth. Columns: `event_id, track_id, parent_track_id, particle, creator_process, creation_time_ns, initial_KE_MeV`.
- `events-truth.csv` — one row per fission event with the fragment PDGs and chain-multiplicity columns (`n_prompt_neutrons`, `n_chain_betas`, etc.). Header line `# n_thermal_neutrons=N` records the run's primary count.

`MyRunAction` walks up from CWD looking for `nuclear-fission.cc` to resolve the repo root.

**Phase status.** Phases A (geometry/materials/generator/mode dispatch), B (`ScintillatorSD` + `CsvWriter` + RunAction/EventAction wiring), and C (fission watcher in `MySteppingAction` populating `EventRecord.fissionTimeNs` / prompt-n/γ multiplicities / fragment PDGs) are all complete. `MySteppingAction::UserSteppingAction` matches the HP fission post-step process `"nFissionHP"` (set by QGSP_BIC_HP, overriding the `G4NeutronFissionProcess` default `"nFission"`); a single-shot `G4cout` of the matched process name on the first fission step of every run sits in the source as a permanent self-check.

`analysis/ground-truth-analysis/` consumes the source-stage CSVs and produces per-run / per-event PDFs into `plots/<ts>_plots/`:
- `plot_energy_spectra.py` → `energy-spectra/` (6 PDFs, per-particle-category KE histograms — neutron/gamma/beta/alpha/ion/other).
- `plot_event_multiplicities.py` → `multiplicities/` (8 PDFs, prompt + chain n/γ + chain β/α/ion/other).
- `plot_decay_schemes.py` → `decay-schemes/` (NNDC/ENSDF nuclear decay scheme per fission event, TikZ-rendered via `pdflatex` with the Jinja2-templated `decay_scheme_template.tex.j2`; one PDF per event).
- `plot_decay_trees.py` → `decay-trees/` (graphviz LR decay tree per fission event, complementary "no-overlap-by-construction" view; one PDF per event).

`plot_decay_schemes.py` requires `pdflatex` on `$PATH`; `plot_decay_trees.py` requires the `dot` binary. Both share chain-shaping (`walk_chain` → `filter_chain_emissions` → `coalesce_isomers`) defined in `plot_decay_schemes.py` and imported by `plot_decay_trees.py`. `analysis/daq/` (`psd.py`, `scintillation_temporal_analysis.py`) are detector-stage placeholders, not yet wired up.

**Analysis-script convention.** Every script in `analysis/` includes a runnable example invocation in its module docstring, under an `Example:` line. The example shows the exact terminal command (interpreter, path-to-script, positional/optional args) and notes the working directory if it matters. Existing ground-truth scripts all resolve the repo root via `find_repo_root(Path(__file__).resolve().parent)`, so they can be invoked from anywhere — make a note of that ("runs from anywhere") in the example. New analysis scripts should follow the same pattern: doc-string includes a one-paragraph description of what the script produces, then a short `Example:` block with a copy-pasteable command.

Photon scoring, SiPM/PMT models, the trigger scintillator (design.md §1.C), and intrinsic LaBr₃ backgrounds remain deferred.

## What you'll see in the OGL viewer

Per the README: green = neutral (prompt fission neutrons + gammas, escape to world boundary), blue stubs at the foil = fission fragments stopping in microns (correct — they are heavy highly-charged ions), red = electrons / betas from the fragment decay chains. Per-event asymmetry is just momentum conservation of one fission, not a bug.

## Documentation conventions

Documentation lives in `doc/`. Three files, each with a distinct audience and scope — keep content in the right one:

- **`doc/design.md`** — detector + DAQ design spec. What an experimental physicist would want to know to understand or replicate the setup: target foil and scintillator geometry, materials and their measured properties, detector array layout, source configuration, expected signal characteristics, scoring/observables. Some detector-relevant physics belongs here, but only at the level needed to justify a design choice — deeper derivations live in `theory.md`.
- **`doc/architecture.md`** — Geant4 simulation infrastructure. How the simulation *code* is organized and how data flows through the program: which user-init class does what, where materials are built, how `SteppingAction` / `EventAction` / `RunAction` hand off accumulated data, how scoring reaches `G4AnalysisManager`, how macros and the UI session fit in. Covers both the source and the detector, but from a code-flow / Geant4-API perspective rather than a physics-of-the-apparatus perspective.
- **`doc/theory.md`** — theoretical physics of every process touched by the sim: fission cross sections, prompt and delayed neutron/gamma emission, fragment stopping, scintillation mechanisms, optical transport, photodetector response. Derivations and references go here. Anything in `design.md` that touches detector physics should be a short summary that points to the full treatment in this file.

When writing or extending these documents, classify each step / process / component as belonging to one of two stages, and say so explicitly:

- **Source stage** — neutron-on-uranium and everything physically downstream of it inside the target: fission, prompt neutron + gamma emission, fission-fragment transport and stopping, radioactive decay chains of the fragments. Anything that would still happen if the scintillator were removed.
- **Detector / DAQ stage** — scintillator physics (energy deposition → optical photons via `G4OpticalPhysics`, Birks quenching, photon transport to the photocathode), the photodetector model (e.g. SiPM response — PDE, gain, dark counts, crosstalk, afterpulsing), digitization / triggering, and the offline analysis scripts in `analysis/` that consume the resulting waveforms or ntuples.

This split is load-bearing because the two stages are validated against very different references (ENDF / fission-product yield tables vs. scintillator + SiPM datasheets), and confusing them is the easiest way to write a plausible-looking but wrong explanation. When a process spans both — e.g. a fission-fragment beta entering the scintillator — call out the boundary-crossing explicitly rather than picking one bucket.

**Math formatting in `theory.md`:** all mathematical expressions must be written in LaTeX math syntax, not plain Unicode or ASCII. Use `$...$` for inline math and `$$...$$` (on its own line, with blank lines above and below) for display equations. Verify that every formula compiles correctly in a standard GitHub-flavored Markdown renderer that supports KaTeX/MathJax. Do not use Unicode superscripts/subscripts (², ³, ₀, etc.) inside equations — those belong only in prose text where no LaTeX context is active.
