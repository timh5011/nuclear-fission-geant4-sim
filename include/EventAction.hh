#ifndef MYEVENTACTION_H
#define MYEVENTACTION_H

#include "G4UserEventAction.hh"
#include "EventRecord.hh"

#include <vector>

class G4Event;
class MySteppingAction;
class MyTrackingAction;
class MyRunAction;
class ScintillatorSD;

// =============================================================================
// MyEventAction
// =============================================================================
// Owns the per-event EventRecord and the fission-flag decision.
//
// Lifecycle:
//   • BeginOfEventAction: reset fRecord; set eventId; hand &fRecord to both
//     SteppingAction (fission watcher) and TrackingAction (chain counters).
//     On the first event, lazily look up the two ScintillatorSDs by name
//     and cache their pointers — they are needed at EoEvent for the
//     flush/discard branch.
//   • EndOfEventAction: branch on fRecord.fissionTimeNs.has_value():
//       fission     → flush all SD pending hits to hits.csv,
//                     write fRecord to events-truth.csv,
//                     drain TrackingAction's pending TruthRows to truth-record.csv.
//       no fission  → call DiscardPending on every SD and on TrackingAction;
//                     write nothing to any of the three files.
//
// This class is the single decision point for "did this event fission?";
// every other component just buffers locally during the event.
// =============================================================================
class MyEventAction : public G4UserEventAction {
public:
    MyEventAction(MySteppingAction* stepping,
                  MyTrackingAction* tracking,
                  MyRunAction*      run);
    ~MyEventAction() override;

    void BeginOfEventAction(const G4Event* event) override;
    void EndOfEventAction  (const G4Event* event) override;

private:
    void EnsureSDsLookedUp();   // lazy, one-shot SD pointer cache

    MySteppingAction* fSteppingAction;
    MyTrackingAction* fTrackingAction;
    MyRunAction*      fRunAction;
    EventRecord       fRecord;

    std::vector<ScintillatorSD*> fSDs;       // cached at first BoEvent
    bool                          fSDsCached{false};
};

#endif
