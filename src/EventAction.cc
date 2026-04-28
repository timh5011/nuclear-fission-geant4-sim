#include "EventAction.hh"

#include "CsvWriter.hh"
#include "RunAction.hh"
#include "SteppingAction.hh"

#include "G4Event.hh"

MyEventAction::MyEventAction(MySteppingAction* stepping, MyRunAction* run)
    : fSteppingAction(stepping),
      fRunAction(run) {}

MyEventAction::~MyEventAction() = default;

// -----------------------------------------------------------------------------
// BeginOfEventAction — reset the per-event accumulator
// -----------------------------------------------------------------------------
void MyEventAction::BeginOfEventAction(const G4Event* event) {
    fRecord = EventRecord{};
    fRecord.eventId = event->GetEventID();
    if (fSteppingAction) fSteppingAction->SetEventRecord(&fRecord);
}

// -----------------------------------------------------------------------------
// EndOfEventAction — flush one row to events.csv
// -----------------------------------------------------------------------------
void MyEventAction::EndOfEventAction(const G4Event*) {
    if (fRunAction && fRunAction->GetEventWriter()) {
        fRunAction->GetEventWriter()->WriteRow(fRecord);
    }
}
