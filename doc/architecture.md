# Architecture

A high-level map of how this simulation is wired together and the chronological order in which the Geant4 framework drives it. This document is about *program structure and control flow*, not the physics models — for the physics, see `doc/theory.md` and the block comments in `include/Physics.hh` / `src/DetectorConstruction.cc`.

After **Phase A** (geometry, materials, generator, mode dispatch) the simulation builds and runs cleanly: `./nuclear_fission` opens the OGL viewer with the full detector array, `./nuclear_fission run.mac` runs headless. The action classes (`RunAction`, `EventAction`, `SteppingAction`) remain empty stubs — they're filled in by Phase B (sensitive detectors + `hits.csv`) and Phase C (fission watcher + `events.csv`).

## Component layout

The application is a thin shell on top of the Geant4 user-application pattern. `nuclear-fission.cc` is `main()`; everything else is a user-derived class registered with `G4RunManager`.

```
nuclear-fission.cc                  main() — owns G4RunManager, sets two pre-Initialize flags,
│                                   then dispatches to interactive or headless mode (argc-based)
│
├── MyDetectorConstruction          geometry + materials + optical surfaces
│   ├── DefineMaterials()           air, U-235, EJ-309, LaBr3(Ce), aluminum;
│   │                                  + shared "TyvekWrap" G4OpticalSurface
│   │                                — full G4MaterialPropertiesTables on both scintillators,
│   │                                  per-particle scintillation yields populated for PSD
│   ├── Construct()                 world (2 m³ air) + foil (20 mm × 0.5 µm U-235 disc)
│   ├── BuildEJ309Array()           8 × EJ-309 cylinders + 1 mm Al housings, r=500 mm forward
│   │                                  + G4LogicalSkinSurface(EJ309LV, TyvekWrap)
│   └── BuildLaBr3Array()           2 × LaBr3(Ce) cylinders, r=300 mm at θ=135°
│                                      + G4LogicalSkinSurface(LaBr3LV, TyvekWrap)
│
├── MyPhysicsList                   header-only modular physics list (EM-option4, HP neutron,
│                                   optical, ion, decay, radioactive decay)
│
└── MyActionInitialization          factory that builds the per-run user actions
    ├── MyPrimaryGenerator          single 0.025 eV neutron at (0, 0, -100 mm) → +ẑ
    ├── MyRunAction                 stub — Phase B: opens hits.csv / events.csv, owns writers
    ├── MyEventAction               stub — Phase B: holds EventRecord + propagates pointer to
    │                                       SteppingAction; flushes events.csv at end of event
    └── MySteppingAction            stub — Phase C: matches "nFissionHP" to capture the
                                            fission-vertex time + prompt-n/γ multiplicities +
                                            two fragment PDGs into the EventRecord
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
   4. `MyActionInitialization::Build()` is called — instantiates the generator and the four action classes and hands them to the kernel via `SetUserAction()` (`src/Action.cc`). `MyEventAction` receives a pointer to `MySteppingAction` here so end-of-event code can read per-step accumulators.
5. **Mode dispatch** (`nuclear-fission.cc:68-79`) — the binary serves two workflows from one entry point:
   - `argc >= 2`: treat `argv[1]` as a macro path, run `/control/execute <path>` headless, exit. No vis, no UI prompt. This is the production path that produces CSV output (after Phase B).
   - `argc == 1`: open the OGL viewer + UI prompt. The viewer setup that used to be inlined as `ApplyCommand` calls now lives in `src/macros/vis.mac`, loaded via `/control/execute vis.mac`. `ui->SessionStart()` blocks until the user types `exit`; from then on, *everything* is driven by user commands at the `Idle>` prompt or sourced macros.

### Geometry construction details (rotation gotcha)

`BuildEJ309Array()` and `BuildLaBr3Array()` orient each cylinder so its local +ẑ (the cylinder axis) points radially outward from the foil — flat face normal to the line back to origin. There's a Geant4 convention trap here that's worth flagging:

`G4PVPlacement` stores the `G4RotationMatrix*` argument as the **frame rotation** (world → local for navigation), not the **object rotation** (local → world for orientation). `G4VPhysicalVolume::GetObjectRotation()` returns `frot->inverse()` — confirming the stored rotation is inverted to obtain the object orientation. The natural construction (build R such that `R · ẑ = radial`) gives the object rotation; we then call `rot->invert()` before passing it to `G4PVPlacement`. Without this inversion the cylinders end up mirrored through the world ẑ-axis — visually subtle from a `(1,1,1)` viewpoint, obvious from a top-down view.

Inline code comments at the rotation block document this.

## Pipeline — per `/run/beamOn N` (driven by the kernel)

Once the UI session is live (or a batch macro is executing) and `/run/beamOn N` fires, the run manager enters the nested loop below. Stub hooks are still called every iteration even when their bodies are empty — wiring scoring later means filling them in, not adding new dispatch.

```
/run/beamOn N
│
├── MyRunAction::BeginOfRunAction(run)              [stub — Phase B fills in: opens timestamped
│                                                              data/<UTC>/{hits,events}.csv,
│                                                              injects HitWriter* into each SD]
│
├── for event = 0 .. N-1:
│   │
│   ├── MyEventAction::BeginOfEventAction(event)   [stub — Phase B: resets EventRecord;
│   │                                                       Phase C: propagates &fRecord to
│   │                                                       SteppingAction]
│   │
│   ├── MyPrimaryGenerator::GeneratePrimaries(event)
│   │       → particle gun fires one 0.025 eV neutron from (0,0,-100 mm) along +ẑ
│   │
│   ├── Tracking loop  (kernel-internal, no user code unless hooks are filled)
│   │   │
│   │   ├── pop next track from the stack (primary first, then secondaries LIFO)
│   │   │
│   │   ├── for each step of that track:
│   │   │       physics processes propose step lengths → shortest wins
│   │   │       step is taken; energy deposited; secondaries created and pushed
│   │   │       │
│   │   │       ├── ScintillatorSD::ProcessHits(step)            [Phase B: per-(track,copyNo)
│   │   │       │                                                 accumulator → hits.csv]
│   │   │       │
│   │   │       └── MySteppingAction::UserSteppingAction(step)   [Phase C: matches the
│   │   │                                                          "nFissionHP" post-step process]
│   │   │
│   │   └── repeat until track ends (stops, escapes world, or is killed)
│   │
│   └── MyEventAction::EndOfEventAction(event)     [Phase B writes one events.csv row;
│                                                   ScintillatorSD::EndOfEvent flushes hits]
│
└── MyRunAction::EndOfRunAction(run)               [stub — Phase B closes the writers]
```

Two non-obvious points about this loop:

- **Secondaries don't restart the loop.** The fission fragments, prompt neutrons, and gammas produced by a fission step are pushed onto the same track stack and processed before control returns to the next event. One `/run/beamOn 1` walks the *entire* shower.
- **Vis trajectories are accumulated during tracking and rendered at end-of-event.** That's why the OGL window only updates between events, not during stepping. `vis.mac` sets `endOfEventAction accumulate 10000` and `endOfRunAction accumulate` so trajectories from many events build up in the view (the default `refresh` would wipe the scene after each event; the vis manager's internal safety cap of 100 events is bumped explicitly).

## Where the stubs are and what they unlock

`RunAction`, `EventAction`, and `SteppingAction` exist as empty shells with their virtual methods commented out. The dispatch path above already calls them — filling them in is purely a matter of un-commenting the override and adding logic. `G4AnalysisManager` is already `#include`d in `RunAction.hh`, `EventAction.hh`, and `SteppingAction.hh`, but Phase B will use a custom `CsvWriter` instead so the include is informational at this stage.

Phase B adds three new translation units (`ScintillatorSD.{hh,cc}`, `CsvWriter.{hh,cc}`) plus a `ConstructSDandField()` method on `MyDetectorConstruction` that attaches `ScintillatorSD` instances to the `EJ309LV` and `LaBr3LV` logical volumes. `CMakeLists.txt` globs `src/*.cc` and `include/*.hh` so new files are picked up automatically — but adding files (or editing a `.mac`) requires re-running `cmake`, not just `make`.

## Optical infrastructure

Scintillation, transport, and reflection are **active** in Phase A; only photon
*scoring* is deferred. Concretely:

- **Generated**: `G4OpticalPhysics` is registered in the physics list, `SetScintByParticleType(true)` is set, and both EJ-309 and LaBr₃ carry full `G4MaterialPropertiesTables` with refractive index, absorption length, emission spectrum, time constants, and per-particle yield curves. Scintillation photons *are* produced by `G4Scintillation::PostStepDoIt` whenever a charged particle deposits energy in a scintillator volume.
- **Reflected**: a single shared `G4OpticalSurface` named `"TyvekWrap"` (`unified` model, `dielectric_dielectric` type, `groundfrontpainted` finish, `REFLECTIVITY = 0.98` flat across 1.5–4.5 eV) is built in `DefineMaterials()` and attached as a `G4LogicalSkinSurface` to **both** `EJ309LV` (in `BuildEJ309Array`) and `LaBr3LV` (in `BuildLaBr3Array`). The `groundfrontpainted` finish makes the wrap behave as a Lambertian diffuse reflector with no transmission — photons hitting any face of any scintillator reflect with 98 % probability, ignoring the neighbor material entirely. This is real, running machinery; the visible effect is suppressed only because photons aren't scored. Skin (vs. border) is the right choice here because the wrap is uniform on every face of every cell and one registration covers all 8 EJ-309 placements automatically. When a PMT/SiPM coupling face is eventually added, override that single face with a `G4LogicalBorderSurface`; the skin keeps handling the other faces.
- **Not scored**: there is no photocathode / SiPM / PMT model and no sensitive detector for optical photons. After scintillation + reflection they are eventually absorbed (2 % per skin interaction, plus bulk absorption via `ABSLENGTH`) or escape into the air world (only via faces with no skin — i.e. nowhere, in the current geometry). They do **not** appear in `hits.csv`.
- **Phase B explicitly skips them**: `ScintillatorSD::ProcessHits` will short-circuit for `track->GetParticleDefinition() == G4OpticalPhoton::Definition()` — one line. To enable photon scoring later, remove that early-return, decide where photons should be tallied (a photocathode-surface SD, or a separate "photons-arriving" counter on the scintillator volume), and add the corresponding rows/columns to the output. Materials, the per-particle flag, and the reflective skins do not need to be revisited.

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
- **`data/<UTC-timestamp>/`** (Phase B) — repo-root output directory, one subdirectory per run. Will contain `hits.csv` and `events.csv`. `MyRunAction::BeginOfRunAction` resolves the repo root by walking up from the executable's CWD looking for `nuclear-fission.cc`, then creates the timestamped directory.
- **`analysis/`, `plots/`** — empty placeholders for downstream post-processing of CSV data once Phase B/C land.
