#ifndef RUNACTION_HH
#define RUNACTION_HH

#include "G4UserRunAction.hh"
#include "globals.hh"

#include <filesystem>
#include <memory>

class HitWriter;
class EventWriter;
class TruthRecordWriter;
class G4Run;

// =============================================================================
// MyRunAction
// =============================================================================
// Owns the per-run CSV writers and the timestamped output directory.
//
// Three writers, three CSVs, all under data/<UTC>/:
//   • hits.csv         — ScintillatorSD hits (filtered to fission events)
//   • events-truth.csv — per-event truth metadata + decay-chain summary
//                        (one row per fission event, with a top-of-file
//                        "# n_thermal_neutrons=N" comment line)
//   • truth-record.csv — per-track truth (every non-optical track born in
//                        each fission event)
//
// Lifecycle:
//   • BeginOfRunAction: walks up from CWD to find the repo root (marked by
//     nuclear-fission.cc), creates data/<UTC>/, opens all three writers
//     with their headers. Reads run->GetNumberOfEventToBeProcessed() to
//     embed the requested beamOn count in events-truth.csv's comment line.
//   • EndOfRunAction: resets the unique_ptrs — destructors flush+close.
//
// EventAction queries the writer accessors at EndOfEventAction. SDs and
// MyTrackingAction now buffer locally and are drained by EventAction; they
// no longer hold writer pointers themselves.
// =============================================================================
class MyRunAction : public G4UserRunAction {
public:
    MyRunAction();
    ~MyRunAction() override;

    void BeginOfRunAction(const G4Run*) override;
    void EndOfRunAction  (const G4Run*) override;

    HitWriter*         GetHitWriter()         { return fHitWriter.get(); }
    EventWriter*       GetEventWriter()       { return fEventWriter.get(); }
    TruthRecordWriter* GetTruthRecordWriter() { return fTruthRecordWriter.get(); }

private:
    // Walks up from std::filesystem::current_path() until a directory
    // containing "nuclear-fission.cc" is found. Throws if the marker isn't
    // found before the filesystem root — that means the binary was launched
    // from somewhere outside the repo, in which case "../data/" is meaningless.
    static std::filesystem::path FindRepoRoot();

    // UTC timestamp formatted as YYYYMMDDTHHMMSS via gmtime_r + strftime.
    // Kept in UTC (not local time) so timestamps are unambiguous across
    // machines / DST transitions.
    static std::string UtcTimestamp();

    std::unique_ptr<HitWriter>         fHitWriter;
    std::unique_ptr<EventWriter>       fEventWriter;
    std::unique_ptr<TruthRecordWriter> fTruthRecordWriter;
};

#endif
