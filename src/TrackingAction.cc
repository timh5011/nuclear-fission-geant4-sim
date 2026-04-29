#include "TrackingAction.hh"

#include "CsvWriter.hh"

#include "G4Track.hh"
#include "G4ParticleDefinition.hh"
#include "G4OpticalPhoton.hh"
#include "G4IonTable.hh"
#include "G4VProcess.hh"
#include "G4Event.hh"
#include "G4EventManager.hh"
#include "G4SystemOfUnits.hh"

namespace {
// Resolve a clean particle-name string for the CSV. Mirrors the helper in
// src/ScintillatorSD.cc: ions get the IonTable's clean Z,A name (no
// excitation tag); everything else gets the standard particle name.
G4String ResolveParticleName(const G4ParticleDefinition* pd) {
    if (pd->GetPDGEncoding() > 1'000'000'000) {
        return G4IonTable::GetIonTable()->GetIonName(pd->GetAtomicNumber(),
                                                     pd->GetAtomicMass());
    }
    return pd->GetParticleName();
}

// Initialize an optional<int> to 0 if it's empty, then increment. Used so
// the n_chain_* fields stay nullopt for events that never had any tracks
// (impossible in practice — at minimum the primary neutron is a track —
// but keeps the optional semantics consistent).
inline void IncOptional(std::optional<G4int>& o) {
    if (!o.has_value()) o = 0;
    *o += 1;
}
}  // namespace

MyTrackingAction::MyTrackingAction()  = default;
MyTrackingAction::~MyTrackingAction() = default;

// -----------------------------------------------------------------------------
// PreUserTrackingAction — fires once per track at its start. Buffer a TruthRow
// and update the per-event chain counters on *fEventRecord.
// -----------------------------------------------------------------------------
void MyTrackingAction::PreUserTrackingAction(const G4Track* track) {
    const auto* pd = track->GetParticleDefinition();

    // Optical photons are excluded from both the truth tree and the chain
    // counts. Same activation pattern as ScintillatorSD::ProcessHits — drop
    // this early-return to start logging photons here too.
    if (pd == G4OpticalPhoton::Definition()) return;

    const G4int pdg = pd->GetPDGEncoding();

    TruthRow row;
    row.eventId        = G4EventManager::GetEventManager()
                             ->GetConstCurrentEvent()
                             ->GetEventID();
    row.trackId        = track->GetTrackID();
    row.parentTrackId  = track->GetParentID();   // 0 == primary
    row.particle       = ResolveParticleName(pd);
    row.creatorProcess = track->GetCreatorProcess()
        ? track->GetCreatorProcess()->GetProcessName()
        : G4String("primary");
    row.creationTimeNs = track->GetGlobalTime()    / ns;
    row.initialKE_MeV  = track->GetKineticEnergy() / MeV;
    fPending.push_back(std::move(row));

    if (!fEventRecord) return;

    IncOptional(fEventRecord->nTotalChainTracks);
    if      (pdg == 2112)                IncOptional(fEventRecord->nChainNeutrons);
    else if (pdg == 22)                  IncOptional(fEventRecord->nChainGammas);
    else if (pdg == 11 || pdg == -11)    IncOptional(fEventRecord->nChainBetas);
    else if (pdg == 1000020040)          IncOptional(fEventRecord->nChainAlphas);
    else if (pdg > 1'000'000'000)        IncOptional(fEventRecord->nChainIons);
    else                                 IncOptional(fEventRecord->nChainOther);
}

// -----------------------------------------------------------------------------
// FlushPending / DiscardPending — driven by MyEventAction::EndOfEventAction.
// -----------------------------------------------------------------------------
void MyTrackingAction::FlushPending(TruthRecordWriter* w) {
    if (w) {
        for (const auto& row : fPending) {
            w->WriteRow(row);
        }
    }
    fPending.clear();
}

void MyTrackingAction::DiscardPending() {
    fPending.clear();
}
