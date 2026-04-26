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

Standard Geant4 user-application layout. `nuclear-fission.cc` wires four user-initialization classes into `G4RunManager` and then opens an interactive OGL viewer (no batch mode is set up yet — every run starts a UI session).

The four user classes:

- **`MyDetectorConstruction`** (`src/DetectorConstruction.cc`) — Builds materials in `DefineMaterials()` (called from the constructor, *not* `Construct()`) so material pointers are members. Geometry is a 10 cm half-side air world containing a thin U-235 foil at the origin (0.5 µm × 5 mm × 5 mm). Pure U-235 is built by hand from a `G4Isotope`/`G4Element` because NIST only provides natural uranium. A plastic scintillator material is defined-but-commented — there is no detector volume yet, only the target.
- **`MyPhysicsList`** (`include/Physics.hh`, header-only) — Modular physics list. The header has long block comments documenting *why* each module is registered; preserve those when editing. Hadronic stack is `QGSP_BIC_HP` + `HadronElasticPhysicsHP` (G4NDL/ENDF data for n + ²³⁵U below 20 MeV). EM is `G4EmStandardPhysics_option4`. `G4OpticalPhysics`, `G4RadioactiveDecayPhysics`, and the ion physics modules are all registered, but optical processes are effectively dormant until a material gets a `G4MaterialPropertiesTable` with `SCINTILLATIONYIELD` / `RINDEX` / etc.
- **`MyActionInitialization`** (`src/Action.cc`) — Constructs and registers `MyPrimaryGenerator`, `MySteppingAction`, `MyEventAction`, `MyRunAction`. `MyEventAction` holds a pointer to the stepping action so per-step accumulators can be flushed at end-of-event.
- **`MyPrimaryGenerator`** (`src/Generator.cc`) — Single thermal neutron (0.025 eV) at `(0,0,-1 cm)` aimed `+ẑ` into the foil. No spread / sampling yet.

**Critical runtime flag:** `nuclear-fission.cc` calls `G4ParticleHPManager::GetInstance()->SetProduceFissionFragments(true)` before `Initialize()`. Without this, the HP package deposits fission energy locally instead of producing explicit fission-fragment ion tracks. Confirmation appears in the run log as `Fission fragment production is now activated in HP package for Z = 92, A = 235`.

**Stub state.** `RunAction`, `EventAction`, `SteppingAction` exist as empty shells with their virtual methods commented out, and `G4AnalysisManager` is included but unused. Output histograms / ntuples / scoring are not yet wired. The `analysis/` and `plots/` directories are empty placeholders.

## What you'll see in the OGL viewer

Per the README: green = neutral (prompt fission neutrons + gammas, escape to world boundary), blue stubs at the foil = fission fragments stopping in microns (correct — they are heavy highly-charged ions), red = electrons / betas from the fragment decay chains. Per-event asymmetry is just momentum conservation of one fission, not a bug.
