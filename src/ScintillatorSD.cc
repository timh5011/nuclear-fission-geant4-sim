#include "ScintillatorSD.hh"

#include "CsvWriter.hh"

#include "G4Step.hh"
#include "G4Track.hh"
#include "G4VTouchable.hh"
#include "G4OpticalPhoton.hh"
#include "G4ParticleDefinition.hh"
#include "G4IonTable.hh"
#include "G4VProcess.hh"
#include "G4EventManager.hh"
#include "G4Event.hh"
#include "G4SystemOfUnits.hh"

namespace {
// Resolve a clean particle-name string for the CSV. For ions the default
// G4ParticleDefinition::GetParticleName() returns names like "Mo95[0.0]"
// with the excitation tag — ugly to filter on. G4IonTable::GetIonName(Z, A)
// returns "Mo95" for the ground state, which is what we want for analysis.
G4String ResolveParticleName(const G4ParticleDefinition* pd) {
    if (pd->GetPDGEncoding() > 1'000'000'000) {
        return G4IonTable::GetIonTable()->GetIonName(pd->GetAtomicNumber(),
                                                     pd->GetAtomicMass());
    }
    return pd->GetParticleName();
}
}  // namespace

ScintillatorSD::ScintillatorSD(const G4String&             name,
                               std::vector<G4String>       detectorIds,
                               G4int                       copyNoDepth)
    : G4VSensitiveDetector(name),
      fDetectorIds(std::move(detectorIds)),
      fCopyNoDepth(copyNoDepth) {}

// -----------------------------------------------------------------------------
// Initialize — called by the kernel at the start of each event. EndOfEvent
// + the EventAction-driven flush/discard already clear both buffers, so this
// is belt-and-braces; keeps the SD in a known state if a future change ever
// exits one of those paths early.
// -----------------------------------------------------------------------------
void ScintillatorSD::Initialize(G4HCofThisEvent*) {
    fAcc.clear();
    fPending.clear();
}

// -----------------------------------------------------------------------------
// ProcessHits — invoked for every step of every track inside the sensitive
// logical volume. Captures entry-time + creator-process on the first time we
// see a (trackId, copyNo) key, and accumulates energy deposit on every step.
// -----------------------------------------------------------------------------
G4bool ScintillatorSD::ProcessHits(G4Step* step, G4TouchableHistory*) {
    G4Track* track = step->GetTrack();

    // Optical-photon scoring is deferred (see doc/architecture.md, "Optical
    // infrastructure"). Remove this single line to start logging optical
    // photons alongside charged-particle entries.
    if (track->GetParticleDefinition() == G4OpticalPhoton::Definition()) {
        return false;
    }

    const G4double edep = step->GetTotalEnergyDeposit();

    const G4int trackId = track->GetTrackID();
    const G4int copyNo  = step->GetPreStepPoint()
                              ->GetTouchable()
                              ->GetCopyNumber(fCopyNoDepth);

    const AccumKey key{trackId, copyNo};
    auto [it, inserted] = fAcc.try_emplace(key);
    Accum& a = it->second;

    if (inserted) {
        const auto* pd = track->GetParticleDefinition();
        a.pdg          = pd->GetPDGEncoding();
        a.particleName = ResolveParticleName(pd);
        a.creatorProcess = track->GetCreatorProcess()
            ? track->GetCreatorProcess()->GetProcessName()
            : G4String("primary");
        a.entryTimeNs  = step->GetPreStepPoint()->GetGlobalTime() / ns;
    }

    a.energyDepMeV += edep / MeV;

    return edep > 0.;
}

// -----------------------------------------------------------------------------
// EndOfEvent — convert per-(track, copy) accumulators into pending HitRows.
// Pure-transit entries (zero edep) are dropped: they inflate hits.csv with no
// physics content (they record the boundary cross only), and the analysis is
// energy-deposit driven. NO file I/O happens here — MyEventAction owns the
// fission decision and will call FlushPending() or DiscardPending() once it
// knows whether this event fissioned.
// -----------------------------------------------------------------------------
void ScintillatorSD::EndOfEvent(G4HCofThisEvent*) {
    const G4int eventId = G4EventManager::GetEventManager()
                              ->GetConstCurrentEvent()
                              ->GetEventID();

    for (const auto& [key, a] : fAcc) {
        if (a.energyDepMeV == 0.) continue;

        HitRow row;
        row.eventId        = eventId;
        row.detectorId     = fDetectorIds.at(key.copyNo);
        row.trackId        = key.trackId;
        row.particle       = a.particleName;
        row.creatorProcess = a.creatorProcess;
        row.entryTimeNs    = a.entryTimeNs;
        row.energyDepMeV   = a.energyDepMeV;
        fPending.push_back(std::move(row));
    }

    fAcc.clear();
}

// -----------------------------------------------------------------------------
// FlushPending / DiscardPending — driven by MyEventAction::EndOfEventAction
// after the fission flag is checked. FlushPending writes every pending row
// to hits.csv; DiscardPending drops them. Both clear fPending.
// -----------------------------------------------------------------------------
void ScintillatorSD::FlushPending(HitWriter* w) {
    if (w) {
        for (const auto& row : fPending) {
            w->WriteRow(row);
        }
    }
    fPending.clear();
}

void ScintillatorSD::DiscardPending() {
    fPending.clear();
}
