// nuclear-fission.cc
//
// Entry point. Builds the Geant4 run manager, registers the four user
// initializations (geometry, physics, action), sets two flags that MUST be
// established before runManager->Initialize(), and then dispatches to either
// an interactive OGL/UI session (no command-line args) or a headless batch
// run that executes a macro file (one or more args; argv[1] is the macro
// path).
//
// The argc-based dispatch lives at this level so the same binary serves
// both `./nuclear_fission` (interactive, drives vis.mac for the viewer) and
// `./nuclear_fission run.mac` (batch, produces CSV output once Phase B
// lands). Vis commands have moved out of C++ and into src/macros/vis.mac.

#include <iostream>

#include "G4RunManager.hh"
#include "G4UImanager.hh"
#include "G4UIExecutive.hh"
#include "G4VisExecutive.hh"
#include "G4VisManager.hh"
#include "G4ParticleHPManager.hh"
#include "G4OpticalParameters.hh"

#include "DetectorConstruction.hh"
#include "Physics.hh"
#include "Action.hh"

int main(int argc, char** argv) {
    auto* runManager = new G4RunManager();

    runManager->SetUserInitialization(new MyDetectorConstruction());
    runManager->SetUserInitialization(new MyPhysicsList());
    runManager->SetUserInitialization(new MyActionInitialization());

    // ---------------------------------------------------------------------
    // Two flags that MUST be set before runManager->Initialize().
    // ---------------------------------------------------------------------
    //
    // (1) HP fission-fragment production. Without this, the HP package
    // deposits the fission Q-value locally instead of producing explicit
    // fragment ion tracks. Confirmation appears in the run log:
    //     "Fission fragment production is now activated in HP package
    //      for Z = 92, A = 235"
    G4ParticleHPManager::GetInstance()->SetProduceFissionFragments(true);

    // (2) Per-particle scintillation. In Geant4 v11 this flag moved from
    // G4OpticalPhysics::SetScintillationByParticleType (deprecated/removed)
    // to G4OpticalParameters. Without it, the per-particle yield curves
    // we attach to materials (PROTONSCINTILLATIONYIELD, IONSCINTILLATIONYIELD,
    // YIELD1/YIELD2 ratios) are silently ignored — every particle type
    // would use the same fast/slow split and PSD would be unrecoverable.
    // We're not yet scoring scintillation photons, but enabling this now
    // means materials are "ready to flip on" without revisiting the flag.
    G4OpticalParameters::Instance()->SetScintByParticleType(true);

    runManager->Initialize();

    auto* UI = G4UImanager::GetUIpointer();

    // ---------------------------------------------------------------------
    // Mode dispatch.
    // ---------------------------------------------------------------------
    // argc >= 2  → headless batch: execute argv[1] as a macro and exit.
    //              Standard Geant4 production pattern; no OGL window opens.
    // argc == 1  → interactive: open OGL viewer + UI prompt, drive viewer
    //              via vis.mac. Same workflow as the original code.
    if (argc >= 2) {
        const G4String macro = argv[1];
        UI->ApplyCommand(G4String("/control/execute ") + macro);
        delete runManager;
        return 0;
    }

    auto* ui = new G4UIExecutive(argc, argv);
    auto* visManager = new G4VisExecutive();
    visManager->Initialize();
    UI->ApplyCommand("/control/execute vis.mac");
    ui->SessionStart();

    delete ui;
    delete visManager;
    delete runManager;
    return 0;
}
