#include "RunAction.hh"

#include "CsvWriter.hh"
#include "ScintillatorSD.hh"

#include "G4Run.hh"
#include "G4SDManager.hh"
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
// BeginOfRunAction — build output dir, open writers, inject into SDs
// -----------------------------------------------------------------------------
void MyRunAction::BeginOfRunAction(const G4Run*) {
    const auto root   = FindRepoRoot();
    const auto outDir = root / "data" / UtcTimestamp();
    std::filesystem::create_directories(outDir);

    fHitWriter   = std::make_unique<HitWriter>  (outDir / "hits.csv");
    fEventWriter = std::make_unique<EventWriter>(outDir / "events.csv");

    // Inject HitWriter* into every registered ScintillatorSD. We look the
    // SDs up by name rather than holding pointers — DetectorConstruction
    // owns the SDs and the lookup keeps the ownership boundary clean.
    auto* sdMan = G4SDManager::GetSDMpointer();
    for (const G4String name : { G4String("EJ309SD"), G4String("LaBr3SD") }) {
        auto* base = sdMan->FindSensitiveDetector(name, /*warning=*/true);
        if (auto* sd = dynamic_cast<ScintillatorSD*>(base)) {
            sd->SetHitWriter(fHitWriter.get());
        }
    }

    G4cout << "[MyRunAction] writing run output to " << outDir.string()
           << G4endl;
}

// -----------------------------------------------------------------------------
// EndOfRunAction — flush + close writers
// -----------------------------------------------------------------------------
void MyRunAction::EndOfRunAction(const G4Run*) {
    fEventWriter.reset();
    fHitWriter.reset();
}
