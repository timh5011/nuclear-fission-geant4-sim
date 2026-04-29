#include "RunAction.hh"

#include "CsvWriter.hh"

#include "G4Run.hh"
#include "globals.hh"

#include <array>
#include <chrono>
#include <ctime>
#include <stdexcept>

MyRunAction::MyRunAction()  = default;
MyRunAction::~MyRunAction() = default;

// -----------------------------------------------------------------------------
// FindRepoRoot — walk up from CWD looking for nuclear-fission.cc
// -----------------------------------------------------------------------------
std::filesystem::path MyRunAction::FindRepoRoot() {
    auto cwd = std::filesystem::current_path();
    for (auto p = cwd; ; p = p.parent_path()) {
        if (std::filesystem::exists(p / "nuclear-fission.cc")) {
            return p;
        }
        if (p == p.parent_path()) {  // hit filesystem root
            throw std::runtime_error(
                "MyRunAction: cannot find repo root (nuclear-fission.cc not "
                "found above " + cwd.string() + ")");
        }
    }
}

// -----------------------------------------------------------------------------
// UtcTimestamp — YYYYMMDDTHHMMSS in UTC
// -----------------------------------------------------------------------------
std::string MyRunAction::UtcTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&tt, &tm);
    std::array<char, 32> buf{};
    std::strftime(buf.data(), buf.size(), "%Y%m%dT%H%M%S", &tm);
    return std::string(buf.data());
}

// -----------------------------------------------------------------------------
// BeginOfRunAction — build output dir, open all three writers
// -----------------------------------------------------------------------------
void MyRunAction::BeginOfRunAction(const G4Run* run) {
    const auto root   = FindRepoRoot();
    const auto outDir = root / "data" / UtcTimestamp();
    std::filesystem::create_directories(outDir);

    // The generator fires exactly one thermal neutron per event, so the
    // requested beamOn count == the number of thermal neutrons fired at the
    // foil. If the generator ever changes (e.g. multi-particle source),
    // revisit this and either store the per-event multiplicity here or
    // accumulate it in EventAction.
    const G4int nThermalNeutrons = run->GetNumberOfEventToBeProcessed();

    fHitWriter         = std::make_unique<HitWriter>(outDir / "hits.csv");
    fEventWriter       = std::make_unique<EventWriter>(outDir / "events-truth.csv",
                                                       nThermalNeutrons);
    fTruthRecordWriter = std::make_unique<TruthRecordWriter>(
                                                       outDir / "truth-record.csv");

    G4cout << "[MyRunAction] writing run output to " << outDir.string()
           << G4endl;
}

// -----------------------------------------------------------------------------
// EndOfRunAction — flush + close writers
// -----------------------------------------------------------------------------
void MyRunAction::EndOfRunAction(const G4Run*) {
    fTruthRecordWriter.reset();
    fEventWriter.reset();
    fHitWriter.reset();
}
