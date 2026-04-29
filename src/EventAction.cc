#include "EventAction.hh"

#include "CsvWriter.hh"
#include "RunAction.hh"
#include "SteppingAction.hh"
#include "TrackingAction.hh"
#include "ScintillatorSD.hh"

#include "G4Event.hh"
#include "G4EventManager.hh"
#include "G4SDManager.hh"
#include "globals.hh"

MyEventAction::MyEventAction(MySteppingAction* stepping,
                             MyTrackingAction* tracking,
                             MyRunAction*      run)
    : fSteppingAction(stepping),
      fTrackingAction(tracking),
      fRunAction(run) {}

MyEventAction::~MyEventAction() = default;

// -----------------------------------------------------------------------------
// EnsureSDsLookedUp — populate fSDs once, on first BoEvent. We can't do this
// in the constructor because Action::Build() runs before ConstructSDandField
// has registered the SDs.
// -----------------------------------------------------------------------------
void MyEventAction::EnsureSDsLookedUp() {
    if (fSDsCached) return;
    fSDsCached = true;

    auto* sdMan = G4SDManager::GetSDMpointer();
    for (const G4String name : { G4String("EJ309SD"), G4String("LaBr3SD") }) {
        auto* base = sdMan->FindSensitiveDetector(name, /*warning=*/true);
        if (auto* sd = dynamic_cast<ScintillatorSD*>(base)) {
            fSDs.push_back(sd);
        }
    }
}

// -----------------------------------------------------------------------------
// BeginOfEventAction — reset the per-event record and inject pointers
// -----------------------------------------------------------------------------
void MyEventAction::BeginOfEventAction(const G4Event* event) {
    EnsureSDsLookedUp();

    fRecord = EventRecord{};
    fRecord.eventId = event->GetEventID();
    if (fSteppingAction) fSteppingAction->SetEventRecord(&fRecord);
    if (fTrackingAction) fTrackingAction->SetEventRecord(&fRecord);
}

// -----------------------------------------------------------------------------
// EndOfEventAction — fission-flag branch
// -----------------------------------------------------------------------------
void MyEventAction::EndOfEventAction(const G4Event*) {
    const bool fissioned = fRecord.fissionTimeNs.has_value();

    if (fissioned && fRunAction) {
        auto* hitW   = fRunAction->GetHitWriter();
        auto* eventW = fRunAction->GetEventWriter();
        auto* truthW = fRunAction->GetTruthRecordWriter();

        for (auto* sd : fSDs)         sd->FlushPending(hitW);
        if (eventW)                   eventW->WriteRow(fRecord);
        if (fTrackingAction)          fTrackingAction->FlushPending(truthW);

        // Mark this event as "kept" so the vis manager retains its
        // trajectories for `/vis/reviewKeptEvents` at the Idle> prompt.
        // Cheap in batch mode (no vis manager → no-op on the trajectory
        // store), so this stays unconditional.
        G4EventManager::GetEventManager()->KeepTheCurrentEvent();
    } else {
        for (auto* sd : fSDs)         sd->DiscardPending();
        if (fTrackingAction)          fTrackingAction->DiscardPending();
    }
}
