# Nuclear-Fission Geant4 Sim — Build-Readiness Audit

## What the sim intends to do

A Geant4 v11.4.0 simulation of **thermal-neutron-induced fission on a U-235 foil with plastic scintillator readout** (per `README.md`).

- **Source**: single neutron fired from (0, 0, −1 cm) along +ẑ via `G4ParticleGun` (`src/Generator.cc`).
- **Target**: thin U-235 foil (0.5 μm × 5 mm × 5 mm), custom isotope at 100% enrichment, ρ = 19.1 g/cm³ in a G4_AIR world (`src/DetectorConstruction.cc`).
- **Physics**: EM option4, `G4OpticalPhysics` (ScintillationByParticleType=true), `QGSP_BIC_HP` + `G4HadronElasticPhysicsHP` for n<20 MeV, EM-extra, ion/ion-elastic, stopping, decay, radioactive decay (`include/Physics.hh`).
- **Readout (planned, not yet wired)**: plastic scintillator (`G4_PLASTIC_SC_VINYLTOLUENE`) + `MySensitiveDetector`, `MySteppingAction` accumulating optical-photon energy / light yield / deposited energy, CSV output via `G4AnalysisManager`.
- **Runtime**: interactive OGL session (`nuclear-fission.cc`), no batch macro path wired in (`src/macros/run.mac` exists but is never invoked).

## Can it build as-is?

**No.** Blockers below.

## Build blockers (must fix before `cmake --build`)

### 1. Missing `Detector.hh` / `MySensitiveDetector`
- `nuclear-fission.cc:12`, `include/DetectorConstruction.hh:21`, and `EventAction` path all `#include "Detector.hh"`. File does not exist.
- `MySensitiveDetector` is referenced in `src/Action.cc:11` and `EventAction.{hh,cc}` but never declared.
- Fix: create `include/Detector.hh` + `src/Detector.cc` defining `MySensitiveDetector : public G4VSensitiveDetector`, or remove all references until you add one.

### 2. Physics class name mismatch
- `include/Physics.hh` defines `class PhysicsList` with an inline constructor.
- `src/Physics.cc` defines `MyPhysicsList::MyPhysicsList()` / `~MyPhysicsList()`.
- `nuclear-fission.cc:18` calls `new MyPhysicsList()`.
- Fix: pick one name. Easiest — rename class in `Physics.hh` to `MyPhysicsList`, move the ctor body into `Physics.cc` (or keep it inline and delete `Physics.cc` definitions).

### 3. `DetectorConstruction` will not compile, and has no world volume
- `src/DetectorConstruction.cc:6` initializes `fScintMaterial(nullptr)` but the member is commented out at `include/DetectorConstruction.hh:35`.
- `src/DetectorConstruction.cc:42` returns `physWorld` — never declared. No world `G4Box`, no `G4PVPlacement`, `logicTarget` is never placed.
- Fix: either drop the `fScintMaterial` initializer or restore the member declaration. Build a world volume (e.g. 1 m `G4Box` of `fWorldMaterial`), place `logicTarget` inside it, and return the `G4VPhysicalVolume*` of the world placement.

### 4. `EventAction` header/source divergence
- `src/EventAction.cc:3` sets `fSensitiveDetector(sensitiveDetector)` but the member is commented out in `include/EventAction.hh:29`.
- Constructor signature takes `MySensitiveDetector*` — undefined type (see #1).
- Fix: either restore the `fSensitiveDetector` member + include `Detector.hh`, or drop the second ctor parameter (and update `Action.cc`).

### 5. `Action.cc` uses an undeclared variable
- `src/Action.cc:11` passes `sensitiveDetector` to `MyEventAction` — identifier is undeclared in this translation unit.
- Fix: construct or retrieve (`G4SDManager`) a `MySensitiveDetector*` before the `MyEventAction` ctor, or drop that argument (see #4).

### 6. `SteppingAction` header/source divergence
- `src/SteppingAction.cc:5–7` initializes `totalOpticalPhotonEnergy`, `totalLightYield`, `totalDepositedEnergy`. None declared in `include/SteppingAction.hh`.
- Fix: add the three `G4double` members (plus accessors) to the header, or remove the initializer list.

### 7. `RunAction.hh` uses headers that do not exist in Geant4 v11
- `include/RunAction.hh:8–9` includes `g4csv_defs.hh` and `G4CsvAnalysisManager.hh`. In v11, CSV is done through the unified manager:
  ```cpp
  auto* ana = G4AnalysisManager::Instance();
  ana->SetDefaultFileType("csv");
  ```
- Fix: delete those includes and use `G4AnalysisManager` directly in `BeginOfRunAction` / `EndOfRunAction`.

### 8. CMake target name mismatch
- `CMakeLists.txt:27` references `nuclear_fission.cc` (underscore). Actual source file is `nuclear-fission.cc` (hyphen). Build fails at configure/compile.
- Fix: rename the file or change the CMake line (and the executable target) to match.

### 9. CMake macro glob path is wrong
- `CMakeLists.txt:32` globs `${PROJECT_SOURCE_DIR}/macros/*.mac`. Actual macro is `src/macros/run.mac`.
- Fix: change glob to `${PROJECT_SOURCE_DIR}/src/macros/*.mac`, or move the `macros` directory up one level.

## Non-blocking issues worth noting

### Physics: 0.025 MeV is not thermal
- `src/Generator.cc:20` sets momentum to `0.025*MeV`. Thermal neutrons are `0.025*eV` (~2200 m/s). At 25 keV the ²³⁵U fission cross-section is roughly 1000× smaller than at thermal; fission rate per incident neutron will be low. The README explicitly says "thermal," so this is almost certainly a unit typo.

### Minor style / hygiene
- `include/EventAction.hh:1` uses guard `MYEVENTACTION_H` while the rest of the project uses `*_HH` — harmless but inconsistent.
- `include/EventAction.hh:25` typo: `// irtual void EndOfEventAction`.
- `nuclear-fission.cc` has no batch mode: if `argc > 1` it should execute the macro, otherwise open the UI. `src/macros/run.mac` is never invoked as written.
- Target foil (0.5 μm) is extremely thin; transmission will dominate. Intentional for a "thin target" measurement but worth confirming.

## Files that will need edits

- `nuclear-fission.cc` — class name, `Detector.hh` include
- `CMakeLists.txt` — source filename, macro glob
- `include/Physics.hh` + `src/Physics.cc` — unify on `MyPhysicsList`
- `include/DetectorConstruction.hh` + `src/DetectorConstruction.cc` — member list, world volume, placements
- `include/EventAction.hh` + `src/EventAction.cc` — align members and ctor signature
- `include/SteppingAction.hh` + `src/SteppingAction.cc` — declare the three tally members
- `include/RunAction.hh` — drop pre-v11 CSV headers
- `src/Action.cc` — remove or source the `sensitiveDetector` argument
- **New**: `include/Detector.hh` + `src/Detector.cc` for `MySensitiveDetector` (if the SD readout is kept)
- `src/Generator.cc` — `MeV` → `eV` if "thermal" is intended

## Verification after fixes

1. `cmake -S . -B build && cmake --build build -j` — produces `build/nuclear_fission` (or `nuclear-fission`) with no missing-header/class warnings.
2. `cd build && ./nuclear_fission` — OGL viewer opens showing a visible world + placed U-235 target, no geometry-overlap warnings, `G4NDL` + `G4RadioactiveDecay` datasets load at init.
3. From the UI: `/run/beamOn 1000` and confirm Geant4 logs fission events (`neutronInelastic` → fragments) without segfault.
4. Confirm CSV output appears in `build/` (once `RunAction` is wired via `G4AnalysisManager`).
