#ifndef CSVWRITER_HH
#define CSVWRITER_HH

#include "globals.hh"
#include "EventRecord.hh"

#include <filesystem>
#include <fstream>
#include <mutex>

// =============================================================================
// HitRow / HitWriter / EventWriter
// =============================================================================
// Two RAII CSV writers, one per output file. Construction "creates the file,
// truncating, and writes the header line"; destruction flushes and closes.
// WriteRow is mutex-guarded so the writers are safe to share across worker
// threads when this sim eventually runs MT — in serial Phase B the lock is
// uncontended and effectively free.
//
// Schemas (kept in lockstep with the comments at the top of CsvWriter.cc):
//   hits.csv:    event_id, detector_id, track_id, particle, creator_process,
//                entry_time_ns, energy_dep_MeV
//   events.csv:  event_id, fission_time_ns, n_prompt_neutrons, n_prompt_gammas,
//                fragment_A_PDG, fragment_B_PDG
// =============================================================================

// One row per (track, sensitive-volume) entry, written by ScintillatorSD.
struct HitRow {
    G4int    eventId;
    G4String detectorId;
    G4int    trackId;
    G4String particle;
    G4String creatorProcess;
    G4double entryTimeNs;
    G4double energyDepMeV;
};

class HitWriter {
public:
    explicit HitWriter(const std::filesystem::path& path);
    ~HitWriter();

    HitWriter(const HitWriter&)            = delete;
    HitWriter& operator=(const HitWriter&) = delete;

    void WriteRow(const HitRow& row);

private:
    std::ofstream fOut;
    std::mutex    fMutex;
};

class EventWriter {
public:
    explicit EventWriter(const std::filesystem::path& path);
    ~EventWriter();

    EventWriter(const EventWriter&)            = delete;
    EventWriter& operator=(const EventWriter&) = delete;

    void WriteRow(const EventRecord& rec);

private:
    std::ofstream fOut;
    std::mutex    fMutex;
};

#endif
