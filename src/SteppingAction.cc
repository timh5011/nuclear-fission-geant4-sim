#include "SteppingAction.hh"

#include "G4Step.hh"
#include "G4Track.hh"
#include "G4VProcess.hh"
#include "G4Neutron.hh"
#include "G4Gamma.hh"
#include "G4SystemOfUnits.hh"
#include "globals.hh"

MySteppingAction::MySteppingAction()  = default;
MySteppingAction::~MySteppingAction() = default;

// -----------------------------------------------------------------------------
// UserSteppingAction — fission watcher
// -----------------------------------------------------------------------------
// Match the HP fission post-step process and capture vertex time, prompt
// multiplicities, and the two fragment PDGs into *fEventRecord. Only the
// FIRST fission per event is recorded; later fissions (rare — would require
// secondary-neutron-induced fission of the foil itself) are ignored.
//
// The match string is "nFissionHP" — set explicitly by QGSP_BIC_HP's
// G4NeutronFissionProcess constructor. The G4NeutronFissionProcess.hh default
// is "nFission"; the BIC_HP builder overrides it. A single-shot G4cout below
// logs the matched name once per run so the run log carries proof we matched
// the right thing — silent zero-fission logging is the failure mode the
// process-name gotcha would produce.
// -----------------------------------------------------------------------------
void MySteppingAction::UserSteppingAction(const G4Step* step) {
    if (!fEventRecord) return;
    if (fEventRecord->fissionTimeNs.has_value()) return;

    const auto* postPoint = step->GetPostStepPoint();
    const auto* proc      = postPoint->GetProcessDefinedStep();
    if (!proc) return;
    const G4String& name  = proc->GetProcessName();
    if (name != "nFissionHP") return;

    static G4bool sLogged = false;
    if (!sLogged) {
        G4cout << "[MySteppingAction] first fission step: process=\""
               << name << "\" at t="
               << postPoint->GetGlobalTime() / ns << " ns" << G4endl;
        sLogged = true;
    }

    fEventRecord->fissionTimeNs = postPoint->GetGlobalTime() / ns;

    G4int nNeutrons = 0;
    G4int nGammas   = 0;
    G4int fragA     = 0;
    G4int fragB     = 0;

    const auto* secs = step->GetSecondaryInCurrentStep();
    if (secs) {
        for (const G4Track* sec : *secs) {
            const auto* pd = sec->GetParticleDefinition();
            if (pd == G4Neutron::NeutronDefinition()) { ++nNeutrons; continue; }
            if (pd == G4Gamma::GammaDefinition())     { ++nGammas;   continue; }
            // Geant4 ion-PDG encoding: 10-digit codes 100ZZZAAAI (Z<1000).
            const G4int pdg = pd->GetPDGEncoding();
            if (pdg > 1'000'000'000) {
                if      (fragA == 0) fragA = pdg;
                else if (fragB == 0) fragB = pdg;
            }
        }
    }

    fEventRecord->nPromptNeutrons = nNeutrons;
    fEventRecord->nPromptGammas   = nGammas;
    if (fragA != 0) fEventRecord->fragmentA_PDG = fragA;
    if (fragB != 0) fEventRecord->fragmentB_PDG = fragB;
}
