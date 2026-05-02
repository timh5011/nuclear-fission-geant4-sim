# Architecture

A high-level map of how this simulation is wired together and the chronological order in which the Geant4 framework drives it. This document is about *program structure and control flow*, not the physics models — for the physics, see `doc/theory.md` and the block comments in `include/Physics.hh` / `src/DetectorConstruction.cc`.

After **Phases A + B + C + D** the simulation builds, runs, and writes complete CSV output. `./nuclear_fission` opens the OGL viewer with the full detector array; `./nuclear_fission run.mac` runs headless and produces `data/<UTC>/{hits.csv, events-truth.csv, truth-record.csv}` at the repo root. `MyRunAction`, `MyEventAction`, `MySteppingAction`, and `MyTrackingAction` are all live. All three CSVs are filtered to fission events only — non-fission events emit nothing on any of them. `events-truth.csv` and `truth-record.csv` are pure truth records: they would be byte-identical even with all detectors removed, because the fission watcher matches the HP fission post-step on the **foil** and `MyTrackingAction` records every track as it's created, independent of the detector array.

## Component layout

The application is a thin shell on top of the Geant4 user-application pattern. `nuclear-fission.cc` is `main()`; everything else is a user-derived class registered with `G4RunManager`.

```
nuclear-fission.cc                  main() — owns G4RunManager, sets two pre-Initialize flags,
│                                   then dispatches to interactive or headless mode (argc-based)
│
├── MyDetectorConstruction          geometry + materials + optical surfaces + SD attach
│   ├── DefineMaterials()           air, U-235, EJ-309, LaBr3(Ce), aluminum;
│   │                                  + shared "TyvekWrap" G4OpticalSurface
│   │                                — full G4MaterialPropertiesTables on both scintillators,
│   │                                  per-particle scintillation yields populated for PSD
│   ├── Construct()                 world (2 m³ air) + foil (20 mm × 0.5 µm U-235 disc)
│   ├── BuildEJ309Array()           48 × EJ-309 cylinders + 1 mm Al housings, r=500 mm forward
│   │                                  (6 polar rings × 8 azimuths on θ ∈ [20°,130°];
│   │                                   Fibonacci-spiral layout commented in source)
│   │                                  + G4LogicalSkinSurface(EJ309LV, TyvekWrap)
│   ├── BuildLaBr3Array()           2 × LaBr3(Ce) cylinders, r=300 mm at θ=135°
│   │                                  + G4LogicalSkinSurface(LaBr3LV, TyvekWrap)
│   └── ConstructSDandField()       attaches one ScintillatorSD per scintillator material:
│                                      EJ309SD (copy-no depth 1) on EJ309LV
│                                      LaBr3SD (copy-no depth 0) on LaBr3LV
│
├── MyPhysicsList                   header-only modular physics list (EM-option4, HP neutron,
│                                   optical, ion, decay, radioactive decay)
│
├── ScintillatorSD                  G4VSensitiveDetector — per-(trackId, copyNo) accumulator;
│                                   skips optical photons; converts accumulators to a
│                                   pending HitRow vector at EndOfEvent (no I/O); drained
│                                   by MyEventAction via FlushPending or DiscardPending
│
├── HitWriter / EventWriter /        RAII CSV writers (CsvWriter.{hh,cc}); mutex-guarded
│   TruthRecordWriter                WriteRow; header-on-construct; close on dtor
│
└── MyActionInitialization          factory — builds RunAction first so EventAction can
    │                               take the run pointer (writer access)
    ├── MyPrimaryGenerator          single 0.025 eV neutron at (0, 0, -100 mm) → +ẑ
    ├── MyRunAction                 owns HitWriter + EventWriter + TruthRecordWriter;
    │                               BeginOfRunAction creates data/<UTC>/, opens all three
    │                               writers, embeds run->GetNumberOfEventToBeProcessed()
    │                               in events-truth.csv's "# n_thermal_neutrons=N" line
    ├── MyEventAction               holds EventRecord; lazy SD lookup on first BoEvent;
    │                               BoEvent resets fRecord, sets eventId, and hands &fRecord
    │                               to BOTH SteppingAction (fission watcher) and
    │                               TrackingAction (chain counters); EoEvent branches on
    │                               fRecord.fissionTimeNs.has_value():
    │                                 fission → flush SDs to hits.csv, write events-truth
    │                                           row, drain TrackingAction to truth-record
    │                                 else    → discard pending hits + truth tracks; no
    │                                           writes to any of the three CSVs
    ├── MySteppingAction            fission watcher — matches the HP fission post-step
    │                               process ("nFissionHP") and on the first match per event
    │                               captures fission-vertex time + prompt-n/γ multiplicities
    │                               + two fragment PDGs into *fEventRecord
    └── MyTrackingAction            truth-record builder — PreUserTrackingAction fires
                                    once per track at its start. Skips optical photons;
                                    buffers a TruthRow (event_id, track_id, parent_track_id,
                                    particle, creator_process, creation_time, initial_KE)
                                    and increments per-particle-type chain counters on
                                    *fEventRecord (n_chain_neutrons, _gammas, _betas,
                                    _alphas, _ions, _other, _total). Drained by
                                    MyEventAction at EoEvent.
```

The four user classes are not peers of `main()`; they are *callbacks* the kernel invokes at well-defined points. Understanding the architecture is mostly understanding *when* the kernel calls each of them.

## Pipeline — initialization (one-time, before any event)

Triggered when `nuclear-fission.cc` runs.

1. **`G4RunManager` constructed** (`nuclear-fission.cc:30`). The kernel singleton that owns the geometry, physics, and action registries.
2. **User initializations registered** (`nuclear-fission.cc:32-34`). At this point the run manager only holds *pointers* — nothing has been built yet. `MyDetectorConstruction`'s constructor *does* run here (it pre-builds materials in `DefineMaterials()`), but `Construct()` has not been called yet.
3. **Two pre-Initialize flags set.** Both **must** happen before `runManager->Initialize()`; setting either after is a silent no-op.
   - **HP fission-fragment flag** (`nuclear-fission.cc:45`) — `G4ParticleHPManager::GetInstance()->SetProduceFissionFragments(true)`. Without this, the HP package deposits the fission Q-value locally instead of producing explicit fragment ion tracks. Confirmation appears in the run log as *"Fission fragment production is now activated in HP package for Z = 92, A = 235"*.
   - **Per-particle scintillation flag** (`nuclear-fission.cc:55`) — `G4OpticalParameters::Instance()->SetScintByParticleType(true)`. In Geant4 v11 the API moved off `G4OpticalPhysics` and onto `G4OpticalParameters`. Without this flag, the per-particle MPT keys (`PROTONSCINTILLATIONYIELD1/2`, etc.) we attach to scintillator materials are silently ignored. With the flag on, `G4Scintillation::PostStepDoIt` *throws* fatal `Scint01` if any particle deposits energy in a material that lacks a per-particle yield curve — which is why `DefineMaterials()` populates electron / proton / ion / alpha / deuteron / triton curves on **both** EJ-309 and LaBr₃ even though LaBr₃ has no PSD.
4. **`runManager->Initialize()`** (`nuclear-fission.cc:57`) — the heavy lifting:
   1. `MyDetectorConstruction::DefineMaterials()` was already run in the constructor (`src/DetectorConstruction.cc:48`), so material pointers exist.
   2. `MyDetectorConstruction::Construct()` is called by the kernel — builds the world `G4Box`, the U-235 foil `G4Tubs`, then dispatches to `BuildEJ309Array()` and `BuildLaBr3Array()` for the two scintillator arrays. Each array helper places one shared logical volume per material and uses `copy_no` on each placement to identify the detector (0..47 for EJ-309, 0..1 for LaBr₃) — Phase B's sensitive detector reads this `copy_no` to map to a `detector_id` string.
   3. `MyPhysicsList` constructs all particles, registers each module's processes against those particles, and applies production cuts. `G4OpticalPhysics` picks up the `SetScintByParticleType` flag here.
   4. `MyActionInitialization::Build()` is called — instantiates the generator and the **five** action classes and hands them to the kernel via `SetUserAction()` (`src/Action.cc`). Construction order matters: `MyRunAction`, `MySteppingAction`, and `MyTrackingAction` are all built before `MyEventAction` so that EventAction's constructor can take pointers to all three (for writer access and for buffered-flush invocation at end-of-event).
   5. After Initialize() returns, `MyDetectorConstruction::ConstructSDandField()` has been called automatically by the kernel — `EJ309SD` and `LaBr3SD` are registered with `G4SDManager` and attached to their logical volumes. `MyEventAction` looks them up by name on the first `BeginOfEventAction` and caches the pointers for the EoEvent flush/discard branch.
5. **Mode dispatch** (`nuclear-fission.cc:68-79`) — the binary serves two workflows from one entry point:
   - `argc >= 2`: treat `argv[1]` as a macro path, run `/control/execute <path>` headless, exit. No vis, no UI prompt. This is the production path that produces CSV output.
   - `argc == 1`: open the OGL viewer + UI prompt. The viewer setup that used to be inlined as `ApplyCommand` calls now lives in `src/macros/vis.mac`, loaded via `/control/execute vis.mac`. `ui->SessionStart()` blocks until the user types `exit`; from then on, *everything* is driven by user commands at the `Idle>` prompt or sourced macros.

### Geometry construction details (rotation gotcha)

`BuildEJ309Array()` and `BuildLaBr3Array()` orient each cylinder so its local +ẑ (the cylinder axis) points radially outward from the foil — flat face normal to the line back to origin. There's a Geant4 convention trap here that's worth flagging:

`G4PVPlacement` stores the `G4RotationMatrix*` argument as the **frame rotation** (world → local for navigation), not the **object rotation** (local → world for orientation). `G4VPhysicalVolume::GetObjectRotation()` returns `frot->inverse()` — confirming the stored rotation is inverted to obtain the object orientation. The natural construction (build R such that `R · ẑ = radial`) gives the object rotation; we then call `rot->invert()` before passing it to `G4PVPlacement`. Without this inversion the cylinders end up mirrored through the world ẑ-axis — visually subtle from a `(1,1,1)` viewpoint, obvious from a top-down view.

Inline code comments at the rotation block document this.

## Pipeline — per `/run/beamOn N` (driven by the kernel)

Once the UI session is live (or a batch macro is executing) and `/run/beamOn N` fires, the run manager enters the nested loop below. All four user-action hooks carry their full Phases A + B + C logic.

```
/run/beamOn N
│
├── MyRunAction::BeginOfRunAction(run)
│       → finds repo root by walking up CWD looking for nuclear-fission.cc
│       → builds data/<UTC-YYYYMMDDTHHMMSS>/
│       → reads N = run->GetNumberOfEventToBeProcessed()
│       → opens HitWriter (hits.csv), EventWriter (events-truth.csv, with
│         "# n_thermal_neutrons=N" comment line), TruthRecordWriter (truth-record.csv)
│
├── for event = 0 .. N-1:
│   │
│   ├── MyEventAction::BeginOfEventAction(event)
│   │       → on first call: lazy SD lookup (EJ309SD, LaBr3SD) → fSDs cache
│   │       → resets EventRecord (all optionals → nullopt)
│   │       → sets fRecord.eventId = event->GetEventID()
│   │       → fSteppingAction->SetEventRecord(&fRecord)
│   │       → fTrackingAction->SetEventRecord(&fRecord)
│   │
│   ├── MyPrimaryGenerator::GeneratePrimaries(event)
│   │       → particle gun fires one 0.025 eV neutron from (0,0,-100 mm) along +ẑ
│   │
│   ├── Tracking loop  (kernel-internal)
│   │   │
│   │   ├── pop next track from the stack (primary first, then secondaries LIFO)
│   │   │
│   │   ├── MyTrackingAction::PreUserTrackingAction(track)   [once, at track start]
│   │   │      → optical photons short-circuit (skipped from both outputs)
│   │   │      → buffer TruthRow with event_id, track_id, parent_track_id,
│   │   │        particle (G4IonTable for ions), creator_process,
│   │   │        creation_time_ns, initial_KE_MeV
│   │   │      → bucket the track by PDG and increment the matching n_chain_*
│   │   │        counter on *fEventRecord (plus n_total_chain_tracks)
│   │   │
│   │   ├── for each step of that track:
│   │   │       physics processes propose step lengths → shortest wins
│   │   │       step is taken; energy deposited; secondaries created and pushed
│   │   │       │
│   │   │       ├── ScintillatorSD::ProcessHits(step)
│   │   │       │      → optical photons short-circuit (return false) — deferred
│   │   │       │      → first time we see (trackId, copyNo): capture entry_time_ns,
│   │   │       │        creator_process, particle name (G4IonTable for ions)
│   │   │       │      → every step: accumulate edep
│   │   │       │
│   │   │       └── MySteppingAction::UserSteppingAction(step)
│   │   │              → if post-step process == "nFissionHP" and we haven't already
│   │   │                captured a fission this event:
│   │   │                  fissionTimeNs = postStep->GetGlobalTime() / ns
│   │   │                  iterate step->GetSecondaryInCurrentStep():
│   │   │                    neutrons → ++nPromptNeutrons
│   │   │                    gammas   → ++nPromptGammas
│   │   │                    ions     → fragmentA_PDG / fragmentB_PDG (first two)
│   │   │              → all five values written into *fEventRecord
│   │   │
│   │   └── repeat until track ends (stops, escapes world, or is killed)
│   │
│   ├── ScintillatorSD::EndOfEvent                  [kernel hook, no I/O]
│   │       → for each (trackId, copyNo) accumulator with energyDepMeV > 0:
│   │         resolve detector_id via copy_no → string table; build a HitRow
│   │         and push to fPending (in-memory, not written yet)
│   │
│   └── MyEventAction::EndOfEventAction(event)      [the fission decision]
│           → if fRecord.fissionTimeNs.has_value():
│               for sd in fSDs: sd->FlushPending(hitWriter)   → hits.csv
│               eventWriter->WriteRow(fRecord)                 → events-truth.csv
│               trackingAction->FlushPending(truthWriter)      → truth-record.csv
│               G4EventManager::KeepTheCurrentEvent()         (vis-only; no-op
│                                                              in batch mode —
│                                                              feeds /vis/reviewKeptEvents)
│             else:
│               for sd in fSDs: sd->DiscardPending()
│               trackingAction->DiscardPending()
│               (nothing is written to any of the three CSVs)
│
└── MyRunAction::EndOfRunAction(run)
        → resets all three writer unique_ptrs (dtors flush + close)
```

Two non-obvious points about this loop:

- **Secondaries don't restart the loop.** The fission fragments, prompt neutrons, and gammas produced by a fission step are pushed onto the same track stack and processed before control returns to the next event. One `/run/beamOn 1` walks the *entire* shower.
- **Vis trajectories are accumulated during tracking and rendered at end-of-event.** That's why the OGL window only updates between events, not during stepping. `vis.mac` sets `endOfEventAction refresh` and `endOfRunAction refresh` so non-fission events are wiped from the scene as they finish; only fission events stick around because `MyEventAction::EndOfEventAction` calls `G4EventManager::KeepTheCurrentEvent()` on the fission branch. After a run, `/vis/reviewKeptEvents` at the `Idle>` prompt walks the kept events one at a time (forward-only; type `cont` + Enter to advance, vis commands to adjust the current view, `/vis/abortReviewKeptEvents` then `cont` to bail out). To accumulate every event into the scene instead — useful for visually scanning thousands of trajectories at once — flip `vis.mac` to `endOfEventAction accumulate <N>` (the vis manager's internal safety cap is 100 if N is omitted).

## Phase status and remaining stubs

Phases A + B + C + D are landed. All five user-action classes are live: `MyDetectorConstruction`, `MyRunAction`, `MyEventAction`, `MySteppingAction`, and `MyTrackingAction` carry their full implementations. All three CSVs are fission-filtered — non-fission events emit nothing on any of them. `events-truth.csv` and `truth-record.csv` are pure truth records (would be byte-identical with detectors removed); `hits.csv` is the only detector-dependent output. The `data/<UTC>/` output tree, `ScintillatorSD` ground-truth scoring, the Tyvek/PTFE optical skin, the fission watcher, and the per-track truth builder all work end-to-end.

`G4AnalysisManager` is `#include`d in a few headers from earlier scaffolding. The custom `CsvWriter` is used instead, so those includes are unused — they could be removed in a future cleanup pass.

Deferred items (not stubs — explicit non-goals): photon scoring, SiPM/PMT model, trigger scintillator, intrinsic LaBr₃ backgrounds, multi-fission-per-event handling, delayed-neutron / delayed-gamma counting. See `doc/design.md` and `doc/plan.md` for the activation paths.

`CMakeLists.txt` globs `src/*.cc` and `include/*.hh`, so new files are picked up automatically — but adding files (or editing a `.mac`) requires re-running `cmake`, not just `make`.

## CSV output and run lifecycle

Each `/run/beamOn` produces one timestamped subdirectory under `<repo-root>/data/`. Resolution order:

1. `MyRunAction::BeginOfRunAction` calls `FindRepoRoot()`, which walks up from `std::filesystem::current_path()` looking for a directory containing `nuclear-fission.cc`. Throws if it hits the filesystem root first — that means the binary was launched from outside the repo.
2. `UtcTimestamp()` formats the current UTC time as `%Y%m%dT%H%M%S` (e.g. `20260428T023041`). UTC is chosen so timestamps are unambiguous across machines and DST transitions.
3. `std::filesystem::create_directories(root / "data" / timestamp)` creates the run directory.
4. `HitWriter`, `EventWriter`, and `TruthRecordWriter` are constructed against `hits.csv`, `events-truth.csv`, and `truth-record.csv` in that directory — they truncate-open the file and write the header line(s) in the constructor, flush+close in the destructor. `EventWriter` additionally takes `run->GetNumberOfEventToBeProcessed()` and writes a `# n_thermal_neutrons=N` provenance line above the column header (the generator fires one thermal neutron per event, so beamOn count == neutrons fired).
5. The writers are owned by `MyRunAction` as `std::unique_ptr`s. `EndOfRunAction` resets them, triggering destruction and flush.

`MyEventAction` reaches all three writers via `MyRunAction::GetHitWriter()`, `GetEventWriter()`, and `GetTruthRecordWriter()`. It also caches `ScintillatorSD*` pointers on the first `BeginOfEventAction` (via `G4SDManager::FindSensitiveDetector` by name) so it can drive `FlushPending(HitWriter*)` / `DiscardPending()` at end-of-event. The lookup-by-name pattern keeps the ownership boundary clean — `MyDetectorConstruction` owns the SDs, `MyRunAction` owns the writers, `MyEventAction` only holds raw pointers it doesn't manage.

CSV schemas (kept in lockstep with `CsvWriter.cc` headers):

```
events-truth.csv:
# n_thermal_neutrons=N
event_id, fission_time_ns, n_prompt_neutrons, n_prompt_gammas,
fragment_A_PDG, fragment_B_PDG,
n_total_chain_tracks, n_chain_neutrons, n_chain_gammas, n_chain_betas,
n_chain_alphas, n_chain_ions, n_chain_other

hits.csv:
event_id, detector_id, track_id, particle, creator_process,
entry_time_ns, energy_dep_MeV

truth-record.csv:
event_id, track_id, parent_track_id, particle, creator_process,
creation_time_ns, initial_KE_MeV
```

All three files are filtered to fission events only — non-fission events emit no rows. `MyEventAction::EndOfEventAction` is the single fission decision point: it inspects `fRecord.fissionTimeNs.has_value()` and either flushes every per-event buffer (SDs' `fPending`, TrackingAction's `fPending`, and the `EventRecord` itself) or discards all of them.

`hits.csv` rows: one per `(track, sensitive-volume)` entry with nonzero energy deposit, in fission events only. Pure-transit entries (zero edep) are dropped — they record a boundary cross with no physics content.

`events-truth.csv` rows: one per fission event. The first six columns are populated by `MySteppingAction` from the `nFissionHP` post-step; the seven `n_chain_*` counts are populated by `MyTrackingAction` across the event from each `PreUserTrackingAction` invocation. The `n_chain_*` counts are a **superset** of `n_prompt_*` (a prompt neutron is counted in both `n_prompt_neutrons` and `n_chain_neutrons`); subtract to isolate delayed/secondary contributions.

`truth-record.csv` rows: one per non-optical track born during a fission event. The `parent_track_id == 0` row is the primary thermal neutron (exactly one per fission event); fission fragments and prompt n/γ have `creator_process == "nFissionHP"`; later generations carry `RadioactiveDecay`, `eIoni`, `compt`, `phot`, etc.

`WriteRow` on all three writers is mutex-guarded. In current serial runs the lock is uncontended and effectively free; the guard is forward-compat for an eventual MT switch.

## Mode dispatch

`nuclear-fission.cc:main()` serves two workflows from one binary, dispatched on `argc`:

- `argc >= 2` — treat `argv[1]` as a macro path, run `/control/execute <path>` headless, exit. This is the production path that produces CSV output. `src/macros/run.mac` is the default headless macro (`./nuclear_fission run.mac`).
- `argc == 1` — interactive. Open the OGL viewer + UI prompt; load `src/macros/vis.mac` for scene setup; `ui->SessionStart()` blocks until the user types `exit`. CSV output is still produced for any `/run/beamOn` issued at the prompt because `MyRunAction::BeginOfRunAction` runs regardless of mode.

The dispatch sits *after* `runManager->Initialize()` and *after* both pre-Initialize flags, so headless and interactive runs are functionally identical from the kernel's perspective.

## Optical infrastructure

Scintillation, transport, and reflection are **active** in Phase A; only photon
*scoring* is deferred. Concretely:

- **Generated**: `G4OpticalPhysics` is registered in the physics list, `SetScintByParticleType(true)` is set, and both EJ-309 and LaBr₃ carry full `G4MaterialPropertiesTables` with refractive index, absorption length, emission spectrum, time constants, and per-particle yield curves. Scintillation photons *are* produced by `G4Scintillation::PostStepDoIt` whenever a charged particle deposits energy in a scintillator volume.
- **Reflected**: a single shared `G4OpticalSurface` named `"TyvekWrap"` (`unified` model, `dielectric_dielectric` type, `groundfrontpainted` finish, `REFLECTIVITY = 0.98` flat across 1.5–4.5 eV) is built in `DefineMaterials()` and attached as a `G4LogicalSkinSurface` to **both** `EJ309LV` (in `BuildEJ309Array`) and `LaBr3LV` (in `BuildLaBr3Array`). The `groundfrontpainted` finish makes the wrap behave as a Lambertian diffuse reflector with no transmission — photons hitting any face of any scintillator reflect with 98 % probability, ignoring the neighbor material entirely. This is real, running machinery; the visible effect is suppressed only because photons aren't scored. Skin (vs. border) is the right choice here because the wrap is uniform on every face of every cell and one registration covers all 48 EJ-309 placements automatically. When a PMT/SiPM coupling face is eventually added, override that single face with a `G4LogicalBorderSurface`; the skin keeps handling the other faces.
- **Not scored**: there is no photocathode / SiPM / PMT model and no sensitive detector for optical photons. After scintillation + reflection they are eventually absorbed (2 % per skin interaction, plus bulk absorption via `ABSLENGTH`) or escape into the air world (only via faces with no skin — i.e. nowhere, in the current geometry). They do **not** appear in `hits.csv`.
- **Phase B explicitly skips them**: `ScintillatorSD::ProcessHits` short-circuits for `track->GetParticleDefinition() == G4OpticalPhoton::Definition()` — one line near the top of the method (`return false`). To enable photon scoring later, delete that early-return, decide where photons should be tallied (a photocathode-surface SD, or a separate "photons-arriving" counter on the scintillator volume), and add the corresponding rows/columns to the output. Materials, the per-particle flag, and the reflective skins do not need to be revisited.

## Intrinsic-background TODO

LaBr₃(Ce) carries a non-negligible internal radioactive background that is **not** modelled in Phase A:

- **¹³⁸La** (t½ = 1.05 × 10¹¹ yr, 0.089 % of natural lanthanum): continuous β + 1436 keV γ. Activation path: replace the pure `Ce/La/Br` element mix with a natural-La element (NIST `G4_La` already includes the ¹³⁸La isotopic fraction), and let `G4RadioactiveDecayPhysics` emit β + γ at the correct rate.
- **²²⁷Ac contamination** from the raw material: α peaks at ~1.7–2.5 MeVee plus the full Ac-Th decay chain. Activation path: add a trace ²²⁷Ac fraction to the material mix, OR fire β/α primaries from a `G4GeneralParticleSource` confined to the LaBr₃ logical volume at the appropriate Bq/cm³ rate.

The `LaBr3_Ce` material block in `src/DetectorConstruction.cc::DefineMaterials()` carries an inline comment with this same activation guidance.

## What lives outside the C++ pipeline

- **`src/macros/run.mac`** — headless batch-mode default. Currently `/run/initialize` + `/run/printProgress 10` + `/run/beamOn 100`. Used as `./nuclear_fission run.mac`.
- **`src/macros/vis.mac`** — interactive viewer setup. Loaded automatically by `main()` when no macro argument is given. Sets up OGL, draws the geometry, enables smooth trajectory rendering, and configures `endOfEventAction refresh` so non-fission events don't pile up in the scene. Designed to pair with the `KeepTheCurrentEvent` hook in `MyEventAction::EndOfEventAction` so `/vis/reviewKeptEvents` flips through fission events only — see the workflow comment at the top of the file.
- **`CMakeLists.txt:32-39`** `configure_file`-copies all `src/macros/*.mac` into `build/` at *cmake* time — so editing a macro requires re-running `cmake`, not just `make`. New macros are auto-picked up by the glob.
- **`build/`** — out-of-tree build directory; deleted and recreated freely.
- **`data/<UTC-timestamp>/`** — repo-root output directory, one subdirectory per `/run/beamOn` invocation. Contains `hits.csv` and `events.csv`. `MyRunAction::BeginOfRunAction` resolves the repo root by walking up from the executable's CWD looking for `nuclear-fission.cc`, then creates the timestamped directory. See "CSV output and run lifecycle" above for full details.
- **`analysis/`, `plots/`** — Python post-processing of CSV data and the PDF outputs it produces. See the *Post-simulation analysis* section below.

---

# Post-simulation analysis

Everything below this line is **outside the Geant4 pipeline**. After a run finishes and `data/<UTC>/{hits.csv, events-truth.csv, truth-record.csv}` are on disk, separate Python scripts under `analysis/` consume those files and emit plots under `plots/<UTC>_plots/`. None of this code touches Geant4, the run manager, or any C++ component — it is pure offline post-processing of the CSV outputs documented above.

This section is summary-level. Implementation lives in the scripts themselves and is short enough to read end-to-end without an architectural map.

## Layout

```
analysis/
├── ground-truth-analysis/        consume events-truth.csv + truth-record.csv;
│   │                             do NOT consume hits.csv. The plots they produce
│   │                             would be byte-identical with all detectors removed.
│   ├── plot_energy_spectra.py        per-particle-category initial-KE histograms
│   ├── plot_event_multiplicities.py  per-event multiplicity histograms
│   ├── plot_event_energy_budgets.py  per-event Sankey of fission energy partition
│   └── requirements.txt              pandas, numpy, matplotlib, plotly, kaleido
│
└── daq/                          placeholders for detector-stage analysis
                                  (consume hits.csv); not yet wired up.
```

Each script takes one positional argument — the run timestamp (e.g. `20260430T051304`) — and resolves the repo root by walking up from the script file looking for `nuclear-fission.cc` (same convention as `MyRunAction::BeginOfRunAction`).

## Outputs

For an input run `data/<ts>/`, each ground-truth script writes into one fixed subdirectory of `plots/<ts>_plots/`:

```
plots/<ts>_plots/
├── energy-spectra/         7 PDFs from plot_energy_spectra.py — one per category
│                           (neutron / gamma / beta / alpha / ion / other), plus
│                           an extra energy_spectrum_beta_with_deltas.pdf with NO
│                           creator_process filter, to expose the delta-electron
│                           pile-up that the filtered plot suppresses.
├── multiplicities/         8 PDFs from plot_event_multiplicities.py — prompt and
│                           chain n/γ, plus chain-only β / α / ion / other.
└── energy-budgets/         N PDFs from plot_event_energy_budgets.py (one per
                            fission event), each a two-stage Sankey diagram.
```

## Conventions shared across scripts

- **Particle bucketing** matches the `n_chain_*` columns of `events-truth.csv`: `neutron`, `gamma`, `beta` (`e-`/`e+`), `alpha`, `ion` (any nucleus matching `^[A-Z][a-z]?\d+$`), `other` (anti-νₑ, exotics). Fission-fragment isotopes (Xe137, Cs139, Ba142, …) all collapse into `ion` rather than getting per-species PDFs.
- **EM transport secondaries are filtered out by default.** `plot_energy_spectra.py` (except the `_with_deltas` variant) and `plot_event_energy_budgets.py` apply `creator_process ∈ {primary, nFissionHP, RadioactiveDecay}` and drop everything else (`ionIoni`, `eIoni`, `phot`, `compt`, `eBrem`). Without this filter ~97 % of `truth-record.csv` rows are sub-keV delta electrons from heavy-ion ionization inside the foil; their kinetic energy is already part of the parent fragment's KE budget so plotting them adds no independent information. The double-counting argument is developed in `doc/theory.md`.
- **Per-fission normalization.** `plot_energy_spectra.py` divides each histogram by the number of fission events in the run (`weights = 1 / N_events`), so the y-axis reads *tracks per fission per bin* — i.e. the expected single-fission spectrum, with statistics that scale with the run size. The multiplicity script is not normalized (each entry is already one event).
- **Sankey topology.** `plot_event_energy_budgets.py` uses Plotly + Kaleido for static PDF export. Three node columns: `Total → {Prompt, Decay chain} → 9 leaf buckets` (`Fragments`, `Prompt neutrons`, `Prompt gammas`, `Decay betas`, `Decay gammas`, `Decay alphas`, `Anti-neutrinos`, `Daughter recoils`, `Decay other`). Empty buckets are dropped — no zero-width ribbons. Each ribbon is labeled with bucket name, summed MeV, and particle count.
- **What "Total tracked KE" means in the Sankey.** The source node is the sum of `initial_KE_MeV` over every track in the event that passes the nuclear-process filter — the primary thermal neutron, the two fragments, prompt n/γ, and every track born during fragment β-decays cascading toward stability. This approximates the **full Q-value** of `U-235 + n → stable daughters + emitted particles + photons + neutrinos`, accumulated through the entire simulated decay chain — *not* just the prompt-fission Q-value. The Prompt branch alone is ~182 MeV (fragments ~169 MeV, prompt n ~5 MeV, prompt γ ~7 MeV); the Decay-chain branch typically adds another 15–25 MeV of β KE + decay γ + anti-νₑ as the unstable fragments cascade to stability. The 0.025 eV input thermal-neutron KE is mathematically included in the sum but negligible (~10⁻¹⁰ of the total). Run-level provenance such as `n_thermal_neutrons` is intentionally **not** shown in the per-event Sankey title — only one of those N neutrons caused the event being drawn, so a run-level count carries no meaning at per-event scope. It does still appear in `plot_energy_spectra.py`'s info textbox, where it is the run-size denominator behind the /N_events normalization.

## Open observation — constant per-event fragment KE

Across the test runs accumulated so far (e.g. `data/20260430T051304/`, 11 fission events), every event reports an **identical** total fragment kinetic energy of ~169.1 MeV, despite the events drawing from a range of distinct (A, Z) fragment-PDG pairs. ENDF B-VII fragment-yield data carries a per-pair Q-value spread of several MeV, so a constant value across pairs is suspicious. Two non-exclusive explanations:

- `G4ParticleHPFission` may be sampling **total** fragment KE from a global Q-value distribution decoupled from the specific (A, Z) pair (i.e. mass-yield and TKE are sampled separately and not constrained pairwise).
- The post-step may be assigning each fragment a **fixed** fraction of a fixed total KE, with the per-pair physics absorbed into the prompt-neutron multiplicity instead.

Worth a follow-up trace through `G4ParticleHPFissionData` / `G4ParticleHPFissionFS` (and their Madland–Nix usage) to confirm which mechanism is in play before drawing physics conclusions from per-event fragment KE. Not a blocker for the analysis pipeline — flagged for the next time someone is in the HP source.
