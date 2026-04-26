# Architecture

A high-level map of how this simulation is wired together and the chronological order in which the Geant4 framework drives it. This document is about *program structure and control flow*, not the physics models — for the physics, see the block comments in `include/Physics.hh`.

## Component layout

The application is a thin shell on top of the Geant4 user-application pattern. `nuclear-fission.cc` is `main()`; everything else is a user-derived class registered with `G4RunManager`.

```
nuclear-fission.cc                  main() — owns G4RunManager, builds UI/Vis, hands control to the UI session
│
├── MyDetectorConstruction          geometry + materials (the U-235 foil and air world)
├── MyPhysicsList                   header-only modular physics list (EM, HP neutron, optical, ion, decay)
└── MyActionInitialization          factory that builds the per-run user actions
    ├── MyPrimaryGenerator          single 0.025 eV neutron at (0,0,-1 cm) → +ẑ
    ├── MyRunAction                 stub — hook for per-run setup/teardown (histos, ntuples)
    ├── MyEventAction               stub — holds a pointer to MySteppingAction for end-of-event flush
    └── MySteppingAction            stub — per-step hook for scoring, optical photon counting, etc.
```

The four user classes are not peers of `main()`; they are *callbacks* the kernel invokes at well-defined points. Understanding the architecture is mostly understanding *when* the kernel calls each of them.

## Pipeline — initialization (one-time, before any event)

Triggered when `nuclear-fission.cc` runs.

1. **`G4RunManager` constructed** (`nuclear-fission.cc:15`). The kernel singleton that owns the geometry, physics, and action registries.
2. **User initializations registered** (`nuclear-fission.cc:17-19`). At this point the run manager only holds *pointers* — nothing has been built yet.
3. **HP fission-fragment flag set** (`nuclear-fission.cc:21`). Must happen *before* `Initialize()` so the HP package picks it up when it builds its cross-section tables. Setting it later is a silent no-op.
4. **`runManager->Initialize()`** (`nuclear-fission.cc:23`) — this is where the heavy lifting happens, in this order:
   1. `MyDetectorConstruction::DefineMaterials()` was already run in the constructor (`src/DetectorConstruction.cc:7`), so material pointers exist.
   2. `MyDetectorConstruction::Construct()` is called by the kernel — builds the world `G4Box`, the U-235 target box, and the placements; returns the world `G4VPhysicalVolume*`.
   3. `MyPhysicsList` constructs all particles, registers each module's processes against those particles, and applies production cuts.
   4. `MyActionInitialization::Build()` is called — instantiates the generator and the four action classes and hands them to the kernel via `SetUserAction()` (`src/Action.cc:7`). `MyEventAction` receives a pointer to `MySteppingAction` here so end-of-event code can read per-step accumulators.
5. **UI executive created** (`nuclear-fission.cc:27`). Owns the interactive command prompt; does not yet take input.
6. **Vis manager initialized + commands applied** (`nuclear-fission.cc:29-37`). `G4VisExecutive` is registered, the OGL viewer is opened, viewpoint set, the world drawn, trajectory storage enabled, tracking verbose pinned to 0. These run as Geant4 macro commands through `G4UImanager`.
7. **`ui->SessionStart()`** (`nuclear-fission.cc:39`). Blocks until the user types `exit`. From here on, *everything* is driven by user commands typed at the `Idle>` prompt or sourced from a macro.

## Pipeline — per `/run/beamOn N` (driven by the kernel)

Once the UI session is live and the user issues `/run/beamOn N`, the run manager enters the nested loop below. Stub hooks are still called every iteration even when their bodies are empty — wiring scoring later means filling them in, not adding new dispatch.

```
/run/beamOn N
│
├── MyRunAction::BeginOfRunAction(run)              [stub — currently commented out]
│
├── for event = 0 .. N-1:
│   │
│   ├── MyEventAction::BeginOfEventAction(event)   [stub — currently commented out]
│   │
│   ├── MyPrimaryGenerator::GeneratePrimaries(event)
│   │       → particle gun fires one thermal neutron, vertex pushed onto the event
│   │
│   ├── Tracking loop  (kernel-internal, no user code unless hooks are filled)
│   │   │
│   │   ├── pop next track from the stack (primary first, then secondaries LIFO)
│   │   │
│   │   ├── for each step of that track:
│   │   │       physics processes propose step lengths → shortest wins
│   │   │       step is taken; energy deposited; secondaries created and pushed
│   │   │       │
│   │   │       └── MySteppingAction::UserSteppingAction(step)   [stub]
│   │   │             ← the per-step hook. This is where you read
│   │   │               step->GetTotalEnergyDeposit(),
│   │   │               step->GetSecondaryInCurrentStep(), etc.
│   │   │
│   │   └── repeat until track ends (stops, escapes world, or is killed)
│   │
│   └── MyEventAction::EndOfEventAction(event)     [stub]
│           ← natural place to flush per-event accumulators that
│             MySteppingAction has been filling
│
└── MyRunAction::EndOfRunAction(run)               [stub]
        ← natural place to write histograms / close the output file
```

Two non-obvious points about this loop:

- **Secondaries don't restart the loop.** The fission fragments, prompt neutrons, and gammas produced by a fission step are pushed onto the same track stack and processed before control returns to the next event. One `beamOn 1` event walks the *entire* shower.
- **Vis trajectories are accumulated during tracking and rendered at end-of-event.** That's why the OGL window only updates between events, not during stepping.

## Where the stubs are and what they unlock

`RunAction`, `EventAction`, and `SteppingAction` exist as empty shells with their virtual methods commented out. The dispatch path above already calls them — filling them in is purely a matter of un-commenting the override and adding logic. `G4AnalysisManager` is already `#include`d in `RunAction.hh`, `EventAction.hh`, and `SteppingAction.hh`, so the typical wiring (book histograms in `BeginOfRunAction`, fill in `UserSteppingAction` / `EndOfEventAction`, write in `EndOfRunAction`) drops in without further plumbing.

The plastic scintillator material is defined in `DetectorConstruction::DefineMaterials()` but commented out, and no scintillator volume is placed in `Construct()`. Adding readout means: place the volume, attach a `G4MaterialPropertiesTable` (so `G4OpticalPhysics` becomes non-trivial), and read optical-photon hits in `MySteppingAction`.

## What lives outside the C++ pipeline

- **`src/macros/*.mac`** — `CMakeLists.txt:32-39` `configure_file`-copies these into `build/` at *cmake* time, so editing a macro requires re-running `cmake`, not just `make`. `run.mac` exists but is currently empty.
- **`build/`** — out-of-tree build directory; deleted and recreated freely.
- **`analysis/`, `plots/`** — empty placeholders for downstream post-processing of whatever the (not-yet-written) output stage produces.
