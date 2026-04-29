#include "CsvWriter.hh"

#include <iomanip>
#include <stdexcept>

namespace {
// Stream a double with fixed precision, then restore the stream's flags. The
// fixed/precision manipulators are sticky on std::ofstream — without restore
// they would silently leak into subsequent integer fields and produce e.g.
// "12345.000000" for track_id. We use 6 decimal places: more than enough
// precision for ns-scale times and MeV-scale energy deposits, and a fixed
// number of digits keeps the column easy to grep/awk over.
void WriteFixed(std::ofstream& s, G4double v, int prec = 6) {
    auto savedFlags = s.flags();
    auto savedPrec  = s.precision();
    s << std::fixed << std::setprecision(prec) << v;
    s.flags(savedFlags);
    s.precision(savedPrec);
}
}  // namespace

// -----------------------------------------------------------------------------
// HitWriter
// -----------------------------------------------------------------------------
HitWriter::HitWriter(const std::filesystem::path& path)
    : fOut(path, std::ios::out | std::ios::trunc) {
    if (!fOut) {
        throw std::runtime_error("HitWriter: cannot open " + path.string());
    }
    fOut << "event_id,detector_id,track_id,particle,creator_process,"
            "entry_time_ns,energy_dep_MeV\n";
}

HitWriter::~HitWriter() = default;

void HitWriter::WriteRow(const HitRow& row) {
    std::lock_guard<std::mutex> lock(fMutex);
    fOut << row.eventId       << ','
         << row.detectorId    << ','
         << row.trackId       << ','
         << row.particle      << ','
         << row.creatorProcess << ',';
    WriteFixed(fOut, row.entryTimeNs);
    fOut << ',';
    WriteFixed(fOut, row.energyDepMeV);
    fOut << '\n';
}

// -----------------------------------------------------------------------------
// EventWriter
// -----------------------------------------------------------------------------
EventWriter::EventWriter(const std::filesystem::path& path,
                         G4int nThermalNeutrons)
    : fOut(path, std::ios::out | std::ios::trunc) {
    if (!fOut) {
        throw std::runtime_error("EventWriter: cannot open " + path.string());
    }
    // Provenance line — pandas/polars/numpy.loadtxt all accept comment='#'.
    fOut << "# n_thermal_neutrons=" << nThermalNeutrons << '\n';
    fOut << "event_id,fission_time_ns,n_prompt_neutrons,n_prompt_gammas,"
            "fragment_A_PDG,fragment_B_PDG,"
            "n_total_chain_tracks,n_chain_neutrons,n_chain_gammas,"
            "n_chain_betas,n_chain_alphas,n_chain_ions,n_chain_other\n";
}

EventWriter::~EventWriter() = default;

void EventWriter::WriteRow(const EventRecord& rec) {
    std::lock_guard<std::mutex> lock(fMutex);
    fOut << rec.eventId << ',';
    if (rec.fissionTimeNs)      WriteFixed(fOut, *rec.fissionTimeNs);
    fOut << ',';
    if (rec.nPromptNeutrons)    fOut << *rec.nPromptNeutrons;
    fOut << ',';
    if (rec.nPromptGammas)      fOut << *rec.nPromptGammas;
    fOut << ',';
    if (rec.fragmentA_PDG)      fOut << *rec.fragmentA_PDG;
    fOut << ',';
    if (rec.fragmentB_PDG)      fOut << *rec.fragmentB_PDG;
    fOut << ',';
    if (rec.nTotalChainTracks)  fOut << *rec.nTotalChainTracks;
    fOut << ',';
    if (rec.nChainNeutrons)     fOut << *rec.nChainNeutrons;
    fOut << ',';
    if (rec.nChainGammas)       fOut << *rec.nChainGammas;
    fOut << ',';
    if (rec.nChainBetas)        fOut << *rec.nChainBetas;
    fOut << ',';
    if (rec.nChainAlphas)       fOut << *rec.nChainAlphas;
    fOut << ',';
    if (rec.nChainIons)         fOut << *rec.nChainIons;
    fOut << ',';
    if (rec.nChainOther)        fOut << *rec.nChainOther;
    fOut << '\n';
}

// -----------------------------------------------------------------------------
// TruthRecordWriter
// -----------------------------------------------------------------------------
TruthRecordWriter::TruthRecordWriter(const std::filesystem::path& path)
    : fOut(path, std::ios::out | std::ios::trunc) {
    if (!fOut) {
        throw std::runtime_error(
            "TruthRecordWriter: cannot open " + path.string());
    }
    fOut << "event_id,track_id,parent_track_id,particle,creator_process,"
            "creation_time_ns,initial_KE_MeV\n";
}

TruthRecordWriter::~TruthRecordWriter() = default;

void TruthRecordWriter::WriteRow(const TruthRow& row) {
    std::lock_guard<std::mutex> lock(fMutex);
    fOut << row.eventId       << ','
         << row.trackId       << ','
         << row.parentTrackId << ','
         << row.particle      << ','
         << row.creatorProcess << ',';
    WriteFixed(fOut, row.creationTimeNs);
    fOut << ',';
    WriteFixed(fOut, row.initialKE_MeV);
    fOut << '\n';
}
