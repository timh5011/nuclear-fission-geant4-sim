#ifndef RUNACTION_HH
#define RUNACTION_HH

#include "G4UserRunAction.hh"
#include "globals.hh"

#include <filesystem>
#include <memory>

class HitWriter;
class EventWriter;
class G4Run;

// =============================================================================
// MyRunAction
// =============================================================================
// Owns the per-run CSV writers and the timestamped output directory.
//
// Lifecycle:
//   • BeginOfRunAction: walks up from the executable's CWD to find the repo
//     root (marked by nuclear-fission.cc), creates data/<UTC>/, opens
//     hits.csv + events.csv with their headers, and injects HitWriter* into
//     every ScintillatorSD it can find. Logs the absolute output path so
//     the run log shows where the data went.
//   • EndOfRunAction: resets the unique_ptrs — destructors flush+close.
//
// EventAction queries GetEventWriter() during EndOfEventAction to write its
// per-event row. The hit writer is owned here but not handed out — the SDs
// hold it directly via SetHitWriter, sidestepping the EventAction → SD path
// (SDs would have to look up the EventAction otherwise, which is awkward).
// =============================================================================
class MyRunAction : public G4UserRunAction {
public:
    MyRunAction();
    ~MyRunAction() override;

    void BeginOfRunAction(const G4Run*) override;
    void EndOfRunAction  (const G4Run*) override;

    EventWriter* GetEventWriter() { return fEventWriter.get(); }

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

    std::unique_ptr<HitWriter>   fHitWriter;
    std::unique_ptr<EventWriter> fEventWriter;
};

#endif
