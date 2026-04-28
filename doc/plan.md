# Implementation Plan — Nuclear-Fission Geant4 Simulation per `doc/design.md`

> **Status (2026-04-28):** Phases **A**, **B**, and **C** are all landed. The simulation builds, runs headless or interactive, and writes complete `data/<UTC>/{hits.csv, events.csv}` for every `/run/beamOn`. `events.csv` rows in fission events carry fission-vertex time, prompt-n / prompt-γ multiplicities, and both fragment PDGs; non-fission events have those columns empty. Phase **D** (architecture.md docs sweep) was folded into the per-phase doc updates. The text below is preserved as the original implementation plan — read it as historical for the parts that are done.

## Context

The current sim is a thin Geant4 shell — only the world, the U-235 foil (wrong shape, wrong axis), the physics list, and a single thermal-neutron primary generator are implemented. `RunAction`, `EventAction`, `SteppingAction` are empty stubs. The detector array described in `doc/design.md` (8 EJ-309 organic + 2 LaBr₃(Ce) inorganic scintillators) does not exist. There is no scoring, no output, no batch mode, and the v11 per-particle scintillation flag is not set so PSD would be silently broken even if scintillator volumes were added.

This plan implements the design.md spec to a "**ground-truth scoring**" stage: every particle entering a sensitive scintillator volume is logged as one row in a CSV, with the per-event fission metadata in a sister CSV. Optical photons are *generated* (per-particle MPT loaded, `SetScintByParticleType(true)` called) so the infrastructure is ready, but they are **not** scored yet — the user will turn that on later. No SiPM/PMT/DAQ modeling. No TOF reconstruction inside Geant4. No trigger scintillator (the user may add one later — design.md §1.C is preserved as documentation of intent).

## Binding scope decisions (from user clarifications)

| Topic | Decision |
|---|---|
| PSD strategy | Ground-truth particle scoring only; CSV out. No optical-photon scoring yet. |
| Photodetector | None. |
| Time window | Prompt only (≤ 10 µs). Do **not** register `G4NeutronTrackingCut`. |
| Foil | 20 mm dia × 0.5 µm thick disc, normal ∥ ẑ (per design.md §1.A). |
| Backing plate | Skip. |
| Trigger scintillator | Skip implementation. Keep design.md §1.C intact. |
| EJ-309 housing | Include 1 mm Al shell. **Non-sensitive.** |
| LaBr₃ intrinsic backgrounds | Skip. Add code comment + architecture.md note. |
| Stilbene variant | Skip. |
| Run mode | Both. `argc == 1` → OGL/UI; `argc ≥ 2` → batch. |
| World | 2 m × 2 m × 2 m. |
| Per-particle scintillation flag | `G4OpticalParameters::Instance()->SetScintByParticleType(true)` in `main()` before `Initialize()`. |
| MPT values | Literature/datasheet values, with citations in code comments. |
| Sensitive-detector pattern | One `ScintillatorSD` class shared across same-material placements; copy-number → detector_id. |
| Output dir | `<repo-root>/data/<UTC-YYYYMMDDTHHMMSS>/`. |
| Hits CSV | `hits.csv`. Cols: `event_id, detector_id, track_id, particle, creator_process, entry_time_ns, energy_dep_MeV`. One row per (track, sensitive-volume) entry. `entry_time_ns` is raw `G4Track::GetGlobalTime()`. |
| Events CSV | `events.csv`. Cols: `event_id, fission_time_ns, n_prompt_neutrons, n_prompt_gammas, fragment_A_PDG, fragment_B_PDG`. Empty fields for non-fission events. |
| Particle name format | `G4IonTable::GetIonName(Z,A)` for ions ("Ba140", "Kr92"); `GetParticleName()` otherwise. |
| Default `beamOn` | 100 events in `run.mac`. |
| Code style | Comments explain *why* (non-obvious); citations in MPT block headers. |

## Critical Geant4 v11.4.0 gotchas the implementation MUST respect

1. **`G4OpticalParameters::Instance()->SetScintByParticleType(true)` must be set before `runManager->Initialize()`.** Otherwise per-particle MPT keys (`PROTONSCINTILLATIONYIELD1/2`, etc.) are silently ignored. The existing comment in `Physics.hh:86` ("removed in Geant4 v11") is wrong — the API moved, it wasn't dropped.
2. **HP fission process name — resolved.** The original plan cited `"nFissionHP"` from `G4HadronPhysicsQGSP_BIC_HP.cc:139` while `G4NeutronFissionProcess.hh:57` showed the constructor default `"nFission"`. The QGSP_BIC_HP physics list explicitly overrides the default — the live name is `"nFissionHP"`. Confirmed both at the source level and at runtime: `MySteppingAction` ships a single-shot `G4cout` of the matched process name on the first fission step of every run. Sample run log entry: `[MySteppingAction] first fission step: process="nFissionHP" at t=45725.5 ns`.
3. **`G4ParticleHPManager::SetProduceFissionFragments(true)` already correct in `nuclear-fission.cc:21`** — keep it before `Initialize()`.
4. **Sensitive detectors must be attached in `MyDetectorConstruction::ConstructSDandField()`** (not `Construct()`) for MT-safety. This method does not exist yet; add it.

## Files to add / modify

### NEW files

| Path | Purpose |
|---|---|
| `include/ScintillatorSD.hh`, `src/ScintillatorSD.cc` | `G4VSensitiveDetector` subclass. Per-(track, copy_no) accumulator. Flushes rows via `HitWriter*` in `EndOfEvent`. |
| `include/CsvWriter.hh`, `src/CsvWriter.cc` | `HitWriter` + `EventWriter` classes. RAII file streams, header-on-construct, mutex-guarded `WriteRow`. |
| `src/macros/vis.mac` | OGL/UI vis commands (currently inlined in `main`). |

### MODIFIED files

| Path | Changes |
|---|---|
| `nuclear-fission.cc` | Add `SetScintByParticleType(true)` before `Initialize()`; add argc-based batch/interactive dispatch; move vis commands to `vis.mac`. |
| `include/Physics.hh` | Replace misleading "removed in Geant4 v11" comment with corrected explanation (capability moved to `G4OpticalParameters`, set in `main()`). No other changes. |
| `include/DetectorConstruction.hh`, `src/DetectorConstruction.cc` | Full rewrite: world → 2 m³, foil → 20 mm × 0.5 µm `G4Tubs` along ẑ, EJ-309 array with Al housings, LaBr₃ array, full materials + MPTs, new `ConstructSDandField()`. |
| `src/Generator.cc` | Position → `(0, 0, -100*mm)`, direction → `(0, 0, 1)`. Energy unchanged. |
| `include/RunAction.hh`, `src/RunAction.cc` | Full rewrite: own `HitWriter`/`EventWriter`, build `data/<UTC>/`, inject writers into SDs in `BeginOfRunAction`, close in `EndOfRunAction`. |
| `include/EventAction.hh`, `src/EventAction.cc` | Add `EventRecord` struct member; `BeginOfEventAction` resets it + propagates `&fRecord` to SteppingAction; `EndOfEventAction` writes one row to events.csv. |
| `include/SteppingAction.hh`, `src/SteppingAction.cc` | Implement `UserSteppingAction` as fission watcher: match `"nFissionHP"`, capture fission time + prompt-n/γ counts + two fragment PDGs from `step->GetSecondaryInCurrentStep()`. |
| `src/Action.cc` | Construct RunAction first, pass `MyRunAction*` to EventAction's constructor (for writer access). |
| `src/macros/run.mac` | Default contents: `/run/initialize`, `/run/printProgress 10`, `/run/beamOn 100`. |

### NOT modified (verified correct)

- `CMakeLists.txt` — globs `src/*.cc`, `include/*.hh`, `src/macros/*.mac`. New files picked up automatically. **Re-run `cmake ..` after adding files** (not just `make`).
- `src/Physics.cc` — stays as one-line stub.
- `analysis/`, `plots/` — empty placeholders, untouched.

## Geometry & Material details

### World
- `G4Box "World"`, half-extents 1 m × 1 m × 1 m, `G4_AIR`.

### ²³⁵U foil
- `G4Tubs "Foil"`, `rmax = 10 mm`, `dz = 0.25 µm` (half-thickness), full angle. Material: existing `Uranium235`. Position: origin. Foil normal ∥ ẑ.
- Comment: 0.5 µm matches real electroplated targets (CHAFF/SPIDER); fragments stop in ~7 µm of U so ~half escape each face with near-full KE.

### EJ-309 array (8 × cylinder + 1 mm Al housing)
Positions per design.md §2 table (lines 133–143); spherical (r=500 mm, θ ∈ {30°, 60°, 90°, 120°}, φ as tabled, IDs `EJ309-0` … `EJ309-7`). Each housing oriented so cylinder axis is radial — particle from origin sees max projected area. Use one `G4LogicalVolume` for the EJ-309 liquid (shared) and one for the Al housing (shared); place 8 physical copies of the housing with copy-number = i (the `ScintillatorSD` uses copy-number → detector_id lookup).

### LaBr₃ array (2 × bare cylinder)
Positions per design.md §3 table: r=300 mm, (135°, 45°) and (135°, 225°). Same pattern — one shared logical, two placements with copy-numbers 0 and 1.

### Materials & MPT keys

**EJ-309** (build by hand; NIST has no EJ-309). Approximate as xylene, density 0.959 g/cm³, H 9.5%, C 90.5% by mass. Birks: `material->GetIonisation()->SetBirksConstant(0.11*mm/MeV)`.

MPT keys (literature values; comment block at top of MPT construction cites Eljen EJ-309 datasheet 2017, Brooks NIM 4 (1959) 151, Enqvist NIMA 715 (2013) 79):

| Key | Value | Purpose |
|---|---|---|
| `RINDEX` | 1.57 (flat over photon energy axis 1.5–4.5 eV) | refractive index |
| `ABSLENGTH` | 100 cm flat | bulk absorption (irrelevant until photon scoring on) |
| `SCINTILLATIONCOMPONENT1`, `SCINTILLATIONCOMPONENT2` | normalized spectrum, peak 424 nm | emission spectra |
| `SCINTILLATIONYIELD` | 12300 / MeV (electron-equivalent) | global default |
| `SCINTILLATIONTIMECONSTANT1` | 3.5 ns | fast component |
| `SCINTILLATIONTIMECONSTANT2` | 32 ns | slow component |
| `SCINTILLATIONYIELD1`, `SCINTILLATIONYIELD2` | 0.85 / 0.15 | electron-default fast/slow split |
| `RESOLUTIONSCALE` | 1.0 | Poisson |
| `ELECTRONSCINTILLATIONYIELD` (table) | linear, 1 MeV → 12300 ph | per-particle yield curve |
| `ELECTRONSCINTILLATIONYIELD1/2` | 0.85 / 0.15 | mostly fast |
| `PROTONSCINTILLATIONYIELD` (table) | Birks-quenched: 1 MeV → ~2400 ph; 5 MeV → ~26000; 10 MeV → ~70000 | proton recoil curve |
| `PROTONSCINTILLATIONYIELD1/2` | 0.55 / 0.45 | larger slow fraction → PSD lever |
| `IONSCINTILLATIONYIELD` (table) | very low, Birks-quenched | fragments |
| `IONSCINTILLATIONYIELD1/2` | 0.40 / 0.60 | |
| `ALPHASCINTILLATIONYIELD` + `..1/2` | similar to ION; 0.40 / 0.60 | |

**LaBr₃(Ce)** — built by hand, density 5.08 g/cm³. La 34.85%, Br 60.14%, Ce 5.01% by mass. Birks: `0.00131*mm/MeV` (Moszynski-style).

MPT keys:

| Key | Value |
|---|---|
| `RINDEX` | 1.9 |
| `ABSLENGTH` | 50 cm |
| `SCINTILLATIONCOMPONENT1` | normalized spectrum, peak 380 nm |
| `SCINTILLATIONYIELD` | 63000 / MeV |
| `SCINTILLATIONTIMECONSTANT1` | 16 ns |
| `SCINTILLATIONYIELD1` | 1.0 |
| `RESOLUTIONSCALE` | 1.0 |
(No COMPONENT2 / YIELD2 / TIMECONSTANT2 — single-component, no PSD.)

LaBr₃ material code gets a comment flagging that intrinsic ¹³⁸La (continuous β + 1436 keV γ) and ²²⁷Ac (α peaks) backgrounds are NOT modeled. Note the addition path (mix isotopes + let `G4RadioactiveDecayPhysics` handle, or a `G4GeneralParticleSource` from inside the volume).

## Sensitive Detector — `ScintillatorSD`

```cpp
class ScintillatorSD : public G4VSensitiveDetector {
public:
  ScintillatorSD(const G4String& name,
                 const std::vector<G4String>& detectorIds);
  void SetHitWriter(HitWriter* w) { fHitWriter = w; }
  void Initialize(G4HCofThisEvent*) override;          // clears fAcc
  G4bool ProcessHits(G4Step*, G4TouchableHistory*) override;
  void EndOfEvent(G4HCofThisEvent*) override;          // flushes fAcc to fHitWriter
private:
  struct AccumKey { G4int trackId; G4int copyNo; };    // hash + eq inline
  struct Accum { G4int pdg; G4String creatorProcess;
                 G4double entryTimeNs, energyDepMeV; };
  std::unordered_map<AccumKey, Accum, ...> fAcc;
  std::vector<G4String> fDetectorIds;                  // copy-no indexed
  HitWriter* fHitWriter{nullptr};
};
```

`ProcessHits` skips optical photons via `if (track->GetParticleDefinition() == G4OpticalPhoton::Definition()) return false;` — this is the single line to remove later when photon scoring flips on. Keys are `(trackId, copyNo)` because the same track can re-enter the same logical volume, and one logical volume hosts multiple physical placements. First step in volume captures `entry_time_ns` and `creator_process`; subsequent steps add `edep`. `creator_process` for primary tracks is the literal `"primary"` (per `GetCreatorProcess() == nullptr`).

`EndOfEvent` iterates `fAcc`, skips entries with `energyDepMeV == 0` (pure transits with no deposition), writes one row each via `fHitWriter->WriteRow(...)`. Uses `G4IonTable::GetIonName(Z, A)` for ions (`pdg > 1e9`) and `GetParticleName()` otherwise.

Two SD instances total: `EJ309SD` (8 detector IDs) and `LaBr3SD` (2). Created in `MyDetectorConstruction::ConstructSDandField()`, registered via `SetSensitiveDetector("EJ309LV", sd)` / `("LaBr3LV", sd)`.

## CSV writers

`HitWriter` and `EventWriter` are tiny RAII classes in `CsvWriter.{hh,cc}`. Constructor opens `<outDir>/hits.csv` (resp. `events.csv`) truncating, writes the header line. Destructor flushes + closes. `WriteRow(const HitRow&)` / `WriteRow(const EventRecord&)` is mutex-guarded (no-op cost in serial; future-proofs MT). Empty optional fields are written as `,,` (per user choice).

Path resolution: `MyRunAction::BeginOfRunAction` walks up from `std::filesystem::current_path()` until it finds a directory containing `nuclear-fission.cc` (the repo-root marker), then constructs `<root>/data/<UTC>` with `gmtime_r` + `%Y%m%dT%H%M%S`. `std::filesystem::create_directories` makes the dir. Writers are then created. After they exist, RunAction loops `{"EJ309SD","LaBr3SD"}`, dynamic_casts `G4SDManager::FindSensitiveDetector(name)` to `ScintillatorSD*`, and calls `SetHitWriter(fHitWriter.get())`.

## Action wiring

- **`Action.cc`**: construct `MyRunAction* run` first, then `MyPrimaryGenerator`, `MySteppingAction* step`, `MyEventAction* evt(step, run)`. Register all four with `SetUserAction`.
- **`MyEventAction`**: holds `EventRecord fRecord` + pointers to `step` and `run`. `BeginOfEventAction(e)` resets `fRecord`, sets `fRecord.eventId = e->GetEventID()`, calls `step->SetEventRecord(&fRecord)`. `EndOfEventAction(e)` calls `run->GetEventWriter()->WriteRow(fRecord)`.
- **`MySteppingAction::UserSteppingAction(step)`** is the fission watcher:
  - return immediately if `fEvent->firstFissionSeen` or `fEvent == nullptr`
  - return if `step->GetPostStepPoint()->GetProcessDefinedStep()->GetProcessName() != "nFissionHP"`
  - else: set `firstFissionSeen = true`; capture `fissionTimeNs = postStep->GetGlobalTime()/ns`; iterate `step->GetSecondaryInCurrentStep()` and tally:
    - `n_prompt_neutrons` if name == `"neutron"`
    - `n_prompt_gammas` if name == `"gamma"`
    - first two ion PDGs (`pdg > 1e9`) → `fragmentA_PDG`, `fragmentB_PDG`

## main() — `nuclear-fission.cc`

```cpp
int main(int argc, char** argv) {
  auto* runManager = new G4RunManager();
  runManager->SetUserInitialization(new MyDetectorConstruction());
  runManager->SetUserInitialization(new MyPhysicsList());
  runManager->SetUserInitialization(new MyActionInitialization());

  // MUST be before Initialize(): see comments in Physics.hh.
  G4ParticleHPManager::GetInstance()->SetProduceFissionFragments(true);
  G4OpticalParameters::Instance()->SetScintByParticleType(true);

  runManager->Initialize();
  auto* UI = G4UImanager::GetUIpointer();

  if (argc >= 2) {
    UI->ApplyCommand(G4String("/control/execute ") + argv[1]);
    delete runManager; return 0;
  }
  // interactive
  auto* ui = new G4UIExecutive(argc, argv);
  auto* visManager = new G4VisExecutive(); visManager->Initialize();
  UI->ApplyCommand("/control/execute vis.mac");
  ui->SessionStart();
  delete ui; delete visManager; delete runManager; return 0;
}
```

## Macros

- `src/macros/run.mac`: `/run/initialize`, `/run/printProgress 10`, `/run/beamOn 100`.
- `src/macros/vis.mac` (NEW): `/run/initialize` (idempotent), `/vis/open OGL`, `/vis/viewer/set/viewpointVector 1 1 1`, `/vis/drawVolume`, `/vis/viewer/set/autoRefresh true`, `/vis/scene/add/trajectories smooth`, `/vis/scene/add/hits`, `/tracking/verbose 0`.

## Code documentation pass

Per CLAUDE.md, comments explain *why* (non-obvious) — not what. Specific spots:

- `nuclear-fission.cc`: above each `*->Set*(true)` call, why "before Initialize()"; above the `argc` switch, why batch/interactive dispatch lives at this level.
- `Physics.hh`: replace the misleading line with a corrected explanation (capability moved to `G4OpticalParameters`).
- `DetectorConstruction.cc`: header citation block for MPT values (Eljen, Brooks, Enqvist); over the foil, why 0.5 µm; over the EJ-309 MPT, why per-particle YIELD1/2 even though we don't score photons yet (= ready-to-flip-on for PSD); over the LaBr₃ material, intrinsic-background TODO with specifics; over `ConstructSDandField`, why this method (not `Construct`) for SD attachment; over the cylinder rotation block, why face-toward-origin.
- `ScintillatorSD::ProcessHits`: why `(trackId, copyNo)` keying; why optical photons short-circuit (single line to delete to flip photon scoring on).
- `SteppingAction::UserSteppingAction`: why `"nFissionHP"` not `"nFission"` (the gotcha).
- `RunAction::BeginOfRunAction`: why repo-root walk-up; why writer injection happens here, not at SD construction (lifetime mismatch).
- `CsvWriter.cc`: why mutex now (MT future-proofing).

## `doc/architecture.md` — post-implementation update

After the code lands, edit architecture.md per the existing convention (program structure / control flow, not physics):

1. **Component layout box** (lines 9-19): add `MyScintillatorSD`, `HitWriter`, `EventWriter`, `EventRecord` under EventAction; rewrite `MyDetectorConstruction` line to mention foil + EJ-309 array (with housings) + LaBr₃ array.
2. **Init pipeline** (lines 23-37): insert step for `G4OpticalParameters::SetScintByParticleType(true)` co-equal with the HP fission flag.
3. **Per-event pipeline** (lines 39-76): replace `[stub]` annotations — describe `EventRecord` reset, fission-watcher logic, SD `ProcessHits`/`EndOfEvent`, `EndOfEventAction` → `events.csv`.
4. **NEW section "CSV output and run lifecycle"**: timestamped dir under `data/`, writer ownership in RunAction, SD-injection at run begin.
5. **NEW section "Mode dispatch"**: argc behavior, `vis.mac` vs `run.mac`.
6. **NEW section "Optical infrastructure (deferred)"**: photons are generated, MPT is complete, flag is set, but `ScintillatorSD::ProcessHits` short-circuits photons. Document the exact line to remove to enable photon scoring.
7. **NEW "Intrinsic-background TODO"**: ¹³⁸La / ²²⁷Ac in LaBr₃, where to add.
8. **"Outside the C++ pipeline" section**: mention both macros and the `data/` output tree.

Do **not** edit `design.md`. Trigger scintillator and backing plate sections stay as documentation of intent.

## Verification

Build:
```bash
source /Users/timmymac/Desktop/Professional/HEP/geant4/geant4-v11.4.0-install/bin/geant4.sh
cd build && rm -rf * && cmake .. && make -j
```

Visual sanity (interactive):
```bash
./nuclear_fission        # OGL opens; foil + 8 EJ-309 + 2 LaBr3 visible
# at the Idle> prompt:
/run/beamOn 1            # green tracks → some hit cylinders
/process/list | grep -i fission   # confirm "nFissionHP" appears (gotcha-check)
/process/list | grep -i Scint     # confirm Scintillation listed (PSD-ready)
```

Headless + CSVs:
```bash
./nuclear_fission run.mac
ls ../data/                          # one timestamped dir
wc -l ../data/*/events.csv           # 101 (header + 100)
head ../data/*/hits.csv              # plausible rows
head ../data/*/events.csv            # most rows have empty fission cols (sub-1% interaction); a few have fragment PDGs, fission_time, multiplicities
```

Order-of-magnitude sanity: a fission row should show `n_prompt_neutrons` ∈ {0..6} (mean ≈ 2.43), `n_prompt_gammas` ≈ 6–8, fragments sum to A ≈ 234–236 minus prompt neutrons. PDGs > 1e9, e.g. `1000420950` (Mo-95) and `1000571390` (La-139). Hit rows for a fission event include several γ entries in LaBr₃, possibly a few neutron entries in EJ-309.

If `events.csv` is all-empty (no `nFissionHP` row ever fires), the watcher is matching the wrong process name — see the v11.4.0 gotcha above.

## Phasing

Three landing phases, each independently verifiable:

- **Phase A — geometry, generator, opt-flag, mode dispatch.** Edits: `nuclear-fission.cc`, `Physics.hh`, `DetectorConstruction.{hh,cc}`, `Generator.cc`, `vis.mac` (new), `run.mac` (default contents). Verifies: build OK, OGL shows the new array, headless run completes (no CSV yet).
- **Phase B — sensitive detectors + hits.csv.** Edits: `ScintillatorSD.{hh,cc}` (new), `CsvWriter.{hh,cc}` (new), `RunAction.{hh,cc}`, `EventAction.{hh,cc}` (just wiring), `Action.cc`, `DetectorConstruction.cc` (`ConstructSDandField`). Verifies: `data/<UTC>/hits.csv` populated; `events.csv` exists but with empty metadata.
- **Phase C — fission watcher + events.csv.** Edits: `SteppingAction.{hh,cc}`, `EventAction.cc` (BOEvent propagation). Verifies: `events.csv` has fission rows with sensible multiplicities + fragment PDGs.
- **Phase D — docs.** Edit `doc/architecture.md` per the spec above.

Re-run `cmake ..` (not just `make`) at the start of each phase that adds files or edits a `.mac`.
