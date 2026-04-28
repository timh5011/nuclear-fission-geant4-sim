# Architecture

A high-level map of how this simulation is wired together and the chronological order in which the Geant4 framework drives it. This document is about *program structure and control flow*, not the physics models — for the physics, see `doc/theory.md` and the block comments in `include/Physics.hh` / `src/DetectorConstruction.cc`.

After **Phases A + B + C** the simulation builds, runs, and writes complete CSV output. `./nuclear_fission` opens the OGL viewer with the full detector array; `./nuclear_fission run.mac` runs headless and produces `data/<UTC>/{hits.csv, events.csv}` at the repo root. `MyRunAction`, `MyEventAction`, and `MySteppingAction` are all live — `MySteppingAction` is the fission watcher that populates the metadata columns of `events.csv` for every event in which the foil fissions.

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
│   ├── BuildEJ309Array()           8 × EJ-309 cylinders + 1 mm Al housings, r=500 mm forward
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
│                                   skips optical photons; flushes HitRow per (track, vol)
│                                   at EndOfEvent via injected HitWriter*
│
├── HitWriter / EventWriter          RAII CSV writers (CsvWriter.{hh,cc}); mutex-guarded
│                                   WriteRow; header-on-construct; close on dtor
│
└── MyActionInitialization          factory — builds RunAction first so EventAction can
    │                               take the run pointer (writer access)
    ├── MyPrimaryGenerator          single 0.025 eV neutron at (0, 0, -100 mm) → +ẑ
    ├── MyRunAction                 owns HitWriter + EventWriter; BeginOfRunAction creates
    │                               data/<UTC>/, opens writers, injects HitWriter* into SDs
    ├── MyEventAction               holds EventRecord; BoEvent resets it, sets eventId,
    │                               and hands &fRecord to SteppingAction so the fission
    │                               watcher knows where to write; EoEvent writes one row
    │                               to events.csv (empty fission cells for non-fission events)
    └── MySteppingAction            fission watcher — matches the HP fission post-step
                                    process ("nFissionHP") and on the first match per event
                                    captures fission-vertex time + prompt-n/γ multiplicities
                                    + two fragment PDGs into *fEventRecord
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
   2. `MyDetectorConstruction::Construct()` is called by the kernel — builds the world `G4Box`, the U-235 foil `G4Tubs`, then dispatches to `BuildEJ309Array()` and `BuildLaBr3Array()` for the two scintillator arrays. Each array helper places one shared logical volume per material and uses `copy_no` on each placement to identify the detector (0..7 for EJ-309, 0..1 for LaBr₃) — Phase B's sensitive detector reads this `copy_no` to map to a `detector_id` string.
   3. `MyPhysicsList` constructs all particles, registers each module's processes against those particles, and applies production cuts. `G4OpticalPhysics` picks up the `SetScintByParticleType` flag here.
   4. `MyActionInitialization::Build()` is called — instantiates the generator and the four action classes and hands them to the kernel via `SetUserAction()` (`src/Action.cc`). Construction order matters: `MyRunAction` is built first so `MyEventAction` can take a pointer to it (for `EventWriter` access at end-of-event). `MyEventAction` also takes a `MySteppingAction*` so it can hand the per-event `EventRecord*` to the fission watcher in `BeginOfEventAction`.
   5. After Initialize() returns, `MyDetectorConstruction::ConstructSDandField()` has been called automatically by the kernel — `EJ309SD` and `LaBr3SD` are registered with `G4SDManager` and attached to their logical volumes. They exist with `fHitWriter == nullptr` until `MyRunAction::BeginOfRunAction` injects the writer pointer at the start of the first run.
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
│       → constructs HitWriter (hits.csv) and EventWriter (events.csv)
│       → injects HitWriter* into both ScintillatorSDs by name lookup
│
├── for event = 0 .. N-1:
│   │
│   ├── MyEventAction::BeginOfEventAction(event)
│   │       → resets EventRecord (all optionals → nullopt)
│   │       → sets fRecord.eventId = event->GetEventID()
│   │       → fSteppingAction->SetEventRecord(&fRecord)
│   │
│   ├── MyPrimaryGenerator::GeneratePrimaries(event)
│   │       → particle gun fires one 0.025 eV neutron from (0,0,-100 mm) along +ẑ
│   │
│   ├── Tracking loop  (kernel-internal)
│   │   │
│   │   ├── pop next track from the stack (primary first, then secondaries LIFO)
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
│   ├── ScintillatorSD::EndOfEvent
│   │       → for each (trackId, copyNo) accumulator with energyDepMeV > 0:
│   │         resolve detector_id via copy_no → string table; emit one HitRow
│   │         via fHitWriter->WriteRow(row)
│   │
│   └── MyEventAction::EndOfEventAction(event)
│           → fRunAction->GetEventWriter()->WriteRow(fRecord)
│           → empty optionals serialize as empty CSV cells
│
└── MyRunAction::EndOfRunAction(run)
        → resets fEventWriter / fHitWriter unique_ptrs (dtors flush + close)
```

Two non-obvious points about this loop:

- **Secondaries don't restart the loop.** The fission fragments, prompt neutrons, and gammas produced by a fission step are pushed onto the same track stack and processed before control returns to the next event. One `/run/beamOn 1` walks the *entire* shower.
- **Vis trajectories are accumulated during tracking and rendered at end-of-event.** That's why the OGL window only updates between events, not during stepping. `vis.mac` sets `endOfEventAction accumulate 10000` and `endOfRunAction accumulate` so trajectories from many events build up in the view (the default `refresh` would wipe the scene after each event; the vis manager's internal safety cap of 100 events is bumped explicitly).

## Phase status and remaining stubs

Phases A + B + C are landed. All four user-action classes are live: `MyDetectorConstruction`, `MyRunAction`, `MyEventAction`, and `MySteppingAction` carry their full implementations. Both CSVs are fully populated: `events.csv` rows in fission events have all six columns filled; non-fission events have only `event_id` (the rest are empty cells). The `data/<UTC>/` output tree, `ScintillatorSD` ground-truth scoring, the Tyvek/PTFE optical skin, and the fission watcher all work end-to-end.

`G4AnalysisManager` is `#include`d in a few headers from earlier scaffolding. The custom `CsvWriter` is used instead, so those includes are unused — they could be removed in a future cleanup pass.

Deferred items (not stubs — explicit non-goals): photon scoring, SiPM/PMT model, trigger scintillator, intrinsic LaBr₃ backgrounds, multi-fission-per-event handling, delayed-neutron / delayed-gamma counting. See `doc/design.md` and `doc/plan.md` for the activation paths.

`CMakeLists.txt` globs `src/*.cc` and `include/*.hh`, so new files are picked up automatically — but adding files (or editing a `.mac`) requires re-running `cmake`, not just `make`.

## CSV output and run lifecycle

Each `/run/beamOn` produces one timestamped subdirectory under `<repo-root>/data/`. Resolution order:

1. `MyRunAction::BeginOfRunAction` calls `FindRepoRoot()`, which walks up from `std::filesystem::current_path()` looking for a directory containing `nuclear-fission.cc`. Throws if it hits the filesystem root first — that means the binary was launched from outside the repo.
2. `UtcTimestamp()` formats the current UTC time as `%Y%m%dT%H%M%S` (e.g. `20260428T023041`). UTC is chosen so timestamps are unambiguous across machines and DST transitions.
3. `std::filesystem::create_directories(root / "data" / timestamp)` creates the run directory.
4. `HitWriter` and `EventWriter` are constructed against `hits.csv` and `events.csv` in that directory — they truncate-open the file and write the header line in the constructor, flush+close in the destructor.
5. The writers are owned by `MyRunAction` as `std::unique_ptr`s. `EndOfRunAction` resets them, triggering destruction and flush.

`HitWriter*` injection into the SDs happens in step 4's continuation: `MyRunAction` looks up `"EJ309SD"` and `"LaBr3SD"` via `G4SDManager::FindSensitiveDetector`, dynamic-casts to `ScintillatorSD*`, and calls `SetHitWriter(fHitWriter.get())`. The lookup-by-name pattern keeps the ownership boundary clean — `MyDetectorConstruction` owns the SDs, `MyRunAction` owns the writers, neither holds the other's pointer.

`MyEventAction` reaches the `EventWriter` via `MyRunAction::GetEventWriter()`, which returns the raw pointer from the unique_ptr.

CSV schemas (kept in lockstep with `CsvWriter.cc` headers):

```
hits.csv:    event_id, detector_id, track_id, particle, creator_process,
             entry_time_ns, energy_dep_MeV
events.csv:  event_id, fission_time_ns, n_prompt_neutrons, n_prompt_gammas,
             fragment_A_PDG, fragment_B_PDG
```

`hits.csv` rows: one per `(track, sensitive-volume)` entry with nonzero energy deposit. Pure-transit entries (zero edep) are dropped — they record a boundary cross with no physics content.

`events.csv` rows: one per Geant4 event. Fission-metadata columns are filled by `MySteppingAction` for every event in which the foil fissions; non-fission events leave them as empty cells (consecutive commas). At thermal × 0.5 µm foil the fission probability is ~10⁻³ per neutron, so the vast majority of rows in a typical run carry only `event_id`.

`WriteRow` on both writers is mutex-guarded. In current serial runs the lock is uncontended and effectively free; the guard is forward-compat for an eventual MT switch.

## Mode dispatch

`nuclear-fission.cc:main()` serves two workflows from one binary, dispatched on `argc`:

- `argc >= 2` — treat `argv[1]` as a macro path, run `/control/execute <path>` headless, exit. This is the production path that produces CSV output. `src/macros/run.mac` is the default headless macro (`./nuclear_fission run.mac`).
- `argc == 1` — interactive. Open the OGL viewer + UI prompt; load `src/macros/vis.mac` for scene setup; `ui->SessionStart()` blocks until the user types `exit`. CSV output is still produced for any `/run/beamOn` issued at the prompt because `MyRunAction::BeginOfRunAction` runs regardless of mode.

The dispatch sits *after* `runManager->Initialize()` and *after* both pre-Initialize flags, so headless and interactive runs are functionally identical from the kernel's perspective.

## Optical infrastructure

Scintillation, transport, and reflection are **active** in Phase A; only photon
*scoring* is deferred. Concretely:

- **Generated**: `G4OpticalPhysics` is registered in the physics list, `SetScintByParticleType(true)` is set, and both EJ-309 and LaBr₃ carry full `G4MaterialPropertiesTables` with refractive index, absorption length, emission spectrum, time constants, and per-particle yield curves. Scintillation photons *are* produced by `G4Scintillation::PostStepDoIt` whenever a charged particle deposits energy in a scintillator volume.
- **Reflected**: a single shared `G4OpticalSurface` named `"TyvekWrap"` (`unified` model, `dielectric_dielectric` type, `groundfrontpainted` finish, `REFLECTIVITY = 0.98` flat across 1.5–4.5 eV) is built in `DefineMaterials()` and attached as a `G4LogicalSkinSurface` to **both** `EJ309LV` (in `BuildEJ309Array`) and `LaBr3LV` (in `BuildLaBr3Array`). The `groundfrontpainted` finish makes the wrap behave as a Lambertian diffuse reflector with no transmission — photons hitting any face of any scintillator reflect with 98 % probability, ignoring the neighbor material entirely. This is real, running machinery; the visible effect is suppressed only because photons aren't scored. Skin (vs. border) is the right choice here because the wrap is uniform on every face of every cell and one registration covers all 8 EJ-309 placements automatically. When a PMT/SiPM coupling face is eventually added, override that single face with a `G4LogicalBorderSurface`; the skin keeps handling the other faces.
- **Not scored**: there is no photocathode / SiPM / PMT model and no sensitive detector for optical photons. After scintillation + reflection they are eventually absorbed (2 % per skin interaction, plus bulk absorption via `ABSLENGTH`) or escape into the air world (only via faces with no skin — i.e. nowhere, in the current geometry). They do **not** appear in `hits.csv`.
- **Phase B explicitly skips them**: `ScintillatorSD::ProcessHits` short-circuits for `track->GetParticleDefinition() == G4OpticalPhoton::Definition()` — one line near the top of the method (`return false`). To enable photon scoring later, delete that early-return, decide where photons should be tallied (a photocathode-surface SD, or a separate "photons-arriving" counter on the scintillator volume), and add the corresponding rows/columns to the output. Materials, the per-particle flag, and the reflective skins do not need to be revisited.

## Intrinsic-background TODO

LaBr₃(Ce) carries a non-negligible internal radioactive background that is **not** modelled in Phase A:

- **¹³⁸La** (t½ = 1.05 × 10¹¹ yr, 0.089 % of natural lanthanum): continuous β + 1436 keV γ. Activation path: replace the pure `Ce/La/Br` element mix with a natural-La element (NIST `G4_La` already includes the ¹³⁸La isotopic fraction), and let `G4RadioactiveDecayPhysics` emit β + γ at the correct rate.
- **²²⁷Ac contamination** from the raw material: α peaks at ~1.7–2.5 MeVee plus the full Ac-Th decay chain. Activation path: add a trace ²²⁷Ac fraction to the material mix, OR fire β/α primaries from a `G4GeneralParticleSource` confined to the LaBr₃ logical volume at the appropriate Bq/cm³ rate.

The `LaBr3_Ce` material block in `src/DetectorConstruction.cc::DefineMaterials()` carries an inline comment with this same activation guidance.

## What lives outside the C++ pipeline

- **`src/macros/run.mac`** — headless batch-mode default. Currently `/run/initialize` + `/run/printProgress 10` + `/run/beamOn 100`. Used as `./nuclear_fission run.mac`.
- **`src/macros/vis.mac`** — interactive viewer setup. Loaded automatically by `main()` when no macro argument is given. Sets up OGL, draws the geometry, enables smooth trajectory rendering, configures `accumulate 10000` so trajectories persist across events.
- **`CMakeLists.txt:32-39`** `configure_file`-copies all `src/macros/*.mac` into `build/` at *cmake* time — so editing a macro requires re-running `cmake`, not just `make`. New macros are auto-picked up by the glob.
- **`build/`** — out-of-tree build directory; deleted and recreated freely.
- **`data/<UTC-timestamp>/`** — repo-root output directory, one subdirectory per `/run/beamOn` invocation. Contains `hits.csv` and `events.csv`. `MyRunAction::BeginOfRunAction` resolves the repo root by walking up from the executable's CWD looking for `nuclear-fission.cc`, then creates the timestamped directory. See "CSV output and run lifecycle" above for full details.
- **`analysis/`, `plots/`** — empty placeholders for downstream post-processing of CSV data once Phase B/C land.
