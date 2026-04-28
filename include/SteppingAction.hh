#ifndef STEPPINGACTION_HH
#define STEPPINGACTION_HH

#include "G4UserSteppingAction.hh"
#include "EventRecord.hh"

class G4Step;

// =============================================================================
// MySteppingAction
// =============================================================================
// Phase C fission watcher. On every step, checks whether the post-step process
// is the HP-driven neutron fission process ("nFissionHP" — set by QGSP_BIC_HP,
// see G4HadronPhysicsQGSP_BIC_HP.cc:139; the G4NeutronFissionProcess default
// "nFission" is overridden there). On the first match per event the watcher
// populates the five Phase C fields of *fEventRecord:
//
//   • fissionTimeNs   — global time at the post-step point
//   • nPromptNeutrons — neutron secondaries born at the fission step
//   • nPromptGammas   — gamma secondaries born at the fission step
//   • fragmentA_PDG / fragmentB_PDG — first two ion secondaries' PDG codes
//
// fEventRecord is injected at BeginOfEventAction by MyEventAction. The first-
// fission guard is `fEventRecord->fissionTimeNs.has_value()` — once that's
// set the watcher early-returns until the next event resets the optional.
// =============================================================================
class MySteppingAction : public G4UserSteppingAction {
public:
    MySteppingAction();
    ~MySteppingAction() override;

    void UserSteppingAction(const G4Step*) override;

    void SetEventRecord(EventRecord* r) { fEventRecord = r; }

private:
    EventRecord* fEventRecord{nullptr};
};

#endif
