#ifndef MYEVENTACTION_H
#define MYEVENTACTION_H

#include "G4UserEventAction.hh"
#include "EventRecord.hh"

class G4Event;
class MySteppingAction;
class MyRunAction;

// =============================================================================
// MyEventAction
// =============================================================================
// Owns a per-event EventRecord, resets it at BeginOfEventAction, and writes
// it to events.csv at EndOfEventAction via MyRunAction's EventWriter.
//
// Phase B fills only `eventId` — the remaining EventRecord fields stay
// std::nullopt and serialize as empty CSV cells. Phase C will hand &fRecord
// to MySteppingAction so the fission watcher can populate the rest.
//
// MySteppingAction is held but unused in Phase B; the pointer is kept so the
// constructor signature doesn't churn at the Phase C boundary.
// =============================================================================
class MyEventAction : public G4UserEventAction {
public:
    MyEventAction(MySteppingAction* stepping, MyRunAction* run);
    ~MyEventAction() override;

    void BeginOfEventAction(const G4Event* event) override;
    void EndOfEventAction  (const G4Event* event) override;

private:
    MySteppingAction* fSteppingAction;   // for Phase C — unused in Phase B
    MyRunAction*      fRunAction;        // for writer access in EOEvent
    EventRecord       fRecord;
};

#endif
