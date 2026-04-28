#include "EventAction.hh"

#include "CsvWriter.hh"
#include "RunAction.hh"

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
}

// -----------------------------------------------------------------------------
// EndOfEventAction — flush one row to events.csv
// -----------------------------------------------------------------------------
void MyEventAction::EndOfEventAction(const G4Event*) {
    if (fRunAction && fRunAction->GetEventWriter()) {
        fRunAction->GetEventWriter()->WriteRow(fRecord);
    }
}
