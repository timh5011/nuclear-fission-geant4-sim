#ifndef SCINTILLATORSD_HH
#define SCINTILLATORSD_HH

#include "G4VSensitiveDetector.hh"
#include "globals.hh"

#include "CsvWriter.hh"   // HitRow definition (kept here so fPending can hold it)

#include <unordered_map>
#include <vector>

class HitWriter;
class G4Step;
class G4HCofThisEvent;
class G4TouchableHistory;

// =============================================================================
// ScintillatorSD
// =============================================================================
// Sensitive detector shared across same-material scintillator placements.
// MyDetectorConstruction::ConstructSDandField creates two instances —
// "EJ309SD" attached to the EJ-309 liquid logical volume (24 placements),
// "LaBr3SD" attached to the LaBr₃ logical volume (2 placements).
//
// Per step (ProcessHits): a per-track, per-copy accumulator is updated.
// Entry time and creator process are captured on the first step of a track
// in a given copy; subsequent steps of the same track in the same copy add
// to the energy deposit. At end-of-event the accumulator is *converted into
// a pending HitRow vector* — it is NOT written immediately. MyEventAction
// owns the fission decision: it calls FlushPending(HitWriter*) for fission
// events and DiscardPending() otherwise, so hits.csv only ever sees rows
// from events where the foil fissioned.
//
// Optical photons are short-circuited at the top of ProcessHits — a single
// line that will be removed when scintillation-photon scoring is enabled.
//
// Copy-number resolution: the EJ-309 SD is attached to the LIQUID logical
// volume (EJ309LV), which is itself placed inside the housing logical
// (EJ309HouseLV) which carries copy_no 0..23. So the EJ-309 SD must look one
// level UP the touchable history to get the housing's copy number — that's
// the `copyNoDepth` constructor argument: 1 for EJ-309, 0 for LaBr₃ (the
// LaBr₃ SD is on the bare crystal placed directly in the world).
// =============================================================================
class ScintillatorSD : public G4VSensitiveDetector {
public:
    ScintillatorSD(const G4String&             name,
                   std::vector<G4String>       detectorIds,
                   G4int                       copyNoDepth);

    void   Initialize  (G4HCofThisEvent*) override;   // clears fAcc + fPending
    G4bool ProcessHits (G4Step*, G4TouchableHistory*) override;
    void   EndOfEvent  (G4HCofThisEvent*) override;   // fAcc → fPending (no I/O)

    // EventAction calls exactly one of these per event after the fission
    // decision is known. Both clear fPending; only FlushPending writes.
    void FlushPending  (HitWriter* w);
    void DiscardPending();

private:
    struct AccumKey {
        G4int trackId;
        G4int copyNo;
        bool operator==(const AccumKey& o) const {
            return trackId == o.trackId && copyNo == o.copyNo;
        }
    };
    struct AccumKeyHash {
        std::size_t operator()(const AccumKey& k) const noexcept {
            // Pack the two G4ints into a 64-bit value and hash. trackId and
            // copyNo are both small (< 2³¹), so packing is collision-free.
            return std::hash<std::int64_t>{}(
                (static_cast<std::int64_t>(k.trackId) << 32) ^
                 static_cast<std::int64_t>(static_cast<std::uint32_t>(k.copyNo)));
        }
    };
    struct Accum {
        G4int    pdg{0};
        G4String particleName;     // resolved once on first step (avoid the
                                   // ion-table lookup per step in long tracks)
        G4String creatorProcess;
        G4double entryTimeNs{0.};
        G4double energyDepMeV{0.};
    };

    std::unordered_map<AccumKey, Accum, AccumKeyHash> fAcc;
    std::vector<HitRow>   fPending;         // built at EndOfEvent, drained by EventAction
    std::vector<G4String> fDetectorIds;     // copy_no → detector_id string
    G4int                 fCopyNoDepth;
};

#endif
