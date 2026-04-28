#ifndef EVENTRECORD_HH
#define EVENTRECORD_HH

#include "globals.hh"

#include <optional>

// =============================================================================
// EventRecord
// =============================================================================
// Per-event ground-truth metadata serialized to events.csv (one row per event).
//
// Phase B fills only `eventId` — every other field is std::optional and stays
// empty, producing empty CSV cells. The Phase C fission watcher (in
// MySteppingAction::UserSteppingAction) populates the remaining fields when it
// matches a fission step. Splitting the metadata into optionals lets
// EventWriter render an empty cell as nothing-between-commas without a sentinel.
//
// Header lives in include/ on its own (rather than inside EventAction.hh) so
// SteppingAction can include it in Phase C without pulling in EventAction's
// G4UserEventAction dependency tree.
// =============================================================================
struct EventRecord {
    G4int                   eventId{-1};

    std::optional<G4double> fissionTimeNs;    // Phase C
    std::optional<G4int>    nPromptNeutrons;  // Phase C
    std::optional<G4int>    nPromptGammas;    // Phase C
    std::optional<G4int>    fragmentA_PDG;    // Phase C
    std::optional<G4int>    fragmentB_PDG;    // Phase C
};

#endif
