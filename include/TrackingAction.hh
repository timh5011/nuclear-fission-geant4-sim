#ifndef TRACKINGACTION_HH
#define TRACKINGACTION_HH

#include "G4UserTrackingAction.hh"
#include "globals.hh"

#include "CsvWriter.hh"     // TruthRow
#include "EventRecord.hh"

#include <vector>

class G4Track;
class TruthRecordWriter;

// =============================================================================
// MyTrackingAction
// =============================================================================
// Phase D truth-record builder. PreUserTrackingAction fires once per track at
// the start of its life — perfect place to capture creation metadata that
// would otherwise require walking secondaries from every step.
//
// Two outputs:
//
//   1. truth-record.csv (via fPending → TruthRecordWriter)
//      One row per non-optical track in fission events. Captures
//      (event_id, track_id, parent_track_id, particle, creator_process,
//       creation_time_ns, initial_KE_MeV).
//
//   2. The n_chain_* counters on EventRecord (filled directly via
//      fEventRecord, the same pointer SteppingAction holds).
//
// Optical photons are skipped at the top of PreUserTrackingAction — they're
// excluded from both outputs, mirroring ScintillatorSD's optical-photon
// short-circuit. Removing that early-return is the activation path for
// scoring scintillation-photon counts in the truth tree.
//
// Filtering: like the SDs, this action buffers everything during the event
// and lets MyEventAction decide at EoEvent whether to flush (fission event)
// or discard (non-fission event). Same fission-flag branch as the SDs.
// =============================================================================
class MyTrackingAction : public G4UserTrackingAction {
public:
    MyTrackingAction();
    ~MyTrackingAction() override;

    void PreUserTrackingAction(const G4Track* track) override;

    // EventAction injects the per-event record at BeginOfEventAction (same
    // pointer pattern MySteppingAction uses).
    void SetEventRecord(EventRecord* r) { fEventRecord = r; }

    // EventAction calls exactly one of these per event after the fission
    // decision is known. Both clear fPending; only FlushPending writes.
    void FlushPending  (TruthRecordWriter* w);
    void DiscardPending();

private:
    EventRecord*           fEventRecord{nullptr};
    std::vector<TruthRow>  fPending;
};

#endif
