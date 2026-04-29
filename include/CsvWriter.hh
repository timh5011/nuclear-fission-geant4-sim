#ifndef CSVWRITER_HH
#define CSVWRITER_HH

#include "globals.hh"
#include "EventRecord.hh"

#include <filesystem>
#include <fstream>
#include <mutex>

// =============================================================================
// HitRow / HitWriter / EventWriter / TruthRow / TruthRecordWriter
// =============================================================================
// Three RAII CSV writers, one per output file. Construction "creates the file,
// truncating, and writes the header line"; destruction flushes and closes.
// WriteRow is mutex-guarded so the writers are safe to share across worker
// threads when this sim eventually runs MT — in serial Phase D the lock is
// uncontended and effectively free.
//
// Schemas (kept in lockstep with the comments at the top of CsvWriter.cc).
// All three files are filtered to fission events only — non-fission events
// emit nothing on any of the three.
//
//   events-truth.csv:
//     # n_thermal_neutrons=N             (top-of-file comment line)
//     event_id, fission_time_ns, n_prompt_neutrons, n_prompt_gammas,
//     fragment_A_PDG, fragment_B_PDG,
//     n_total_chain_tracks, n_chain_neutrons, n_chain_gammas,
//     n_chain_betas, n_chain_alphas, n_chain_ions, n_chain_other
//
//   hits.csv:
//     event_id, detector_id, track_id, particle, creator_process,
//     entry_time_ns, energy_dep_MeV
//
//   truth-record.csv:
//     event_id, track_id, parent_track_id, particle, creator_process,
//     creation_time_ns, initial_KE_MeV
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
    // nThermalNeutrons is written as a "# n_thermal_neutrons=N" comment line
    // ahead of the column header so the file carries its own provenance.
    EventWriter(const std::filesystem::path& path, G4int nThermalNeutrons);
    ~EventWriter();

    EventWriter(const EventWriter&)            = delete;
    EventWriter& operator=(const EventWriter&) = delete;

    void WriteRow(const EventRecord& rec);

private:
    std::ofstream fOut;
    std::mutex    fMutex;
};

// One row per non-optical track in fission events, written by MyTrackingAction.
struct TruthRow {
    G4int    eventId;
    G4int    trackId;
    G4int    parentTrackId;
    G4String particle;
    G4String creatorProcess;
    G4double creationTimeNs;
    G4double initialKE_MeV;
};

class TruthRecordWriter {
public:
    explicit TruthRecordWriter(const std::filesystem::path& path);
    ~TruthRecordWriter();

    TruthRecordWriter(const TruthRecordWriter&)            = delete;
    TruthRecordWriter& operator=(const TruthRecordWriter&) = delete;

    void WriteRow(const TruthRow& row);

private:
    std::ofstream fOut;
    std::mutex    fMutex;
};

#endif
