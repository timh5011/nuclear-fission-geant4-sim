#ifndef DETECTORCONSTRUCTION_HH
#define DETECTORCONSTRUCTION_HH

#include "G4VUserDetectorConstruction.hh"
#include "globals.hh"

class G4LogicalVolume;
class G4Material;
class G4VPhysicalVolume;

// =============================================================================
// MyDetectorConstruction
// =============================================================================
//
// Builds the geometry described in doc/design.md:
//   - 2 m × 2 m × 2 m air world
//   - 20 mm dia × 0.5 µm pure ²³⁵U disc (G4Tubs), normal ∥ ẑ
//   - 8 × EJ-309 organic scintillators (50 mm × 50 mm cylinders) at r=500 mm
//     in the forward hemisphere, each in a 1 mm aluminum housing
//   - 2 × LaBr₃(Ce) inorganic scintillators (38 mm × 38 mm cylinders) at
//     r=300 mm, θ=135°, bare (no housing)
//
// Materials carry full G4MaterialPropertiesTables, including per-particle
// scintillation yields. Optical photons are GENERATED but not yet scored —
// the optical infrastructure is intentionally left "ready to flip on" so
// PSD work later doesn't require revisiting materials. The per-particle
// scintillation flag (G4OpticalParameters::SetScintByParticleType) MUST
// be set in main() before runManager->Initialize() — otherwise the
// per-particle MPT keys are silently ignored.
//
// Trigger scintillator and aluminum backing plate (design.md §1.C, §1.D)
// are intentionally NOT placed — the user may add the trigger later.
// =============================================================================
class MyDetectorConstruction : public G4VUserDetectorConstruction {
public:
    MyDetectorConstruction();
    ~MyDetectorConstruction() override;

    G4VPhysicalVolume* Construct() override;

private:
    // Materials are built in the constructor (ahead of Construct()) so that
    // material pointers are already valid when Geant4's kernel calls
    // Construct() during runManager->Initialize().
    void DefineMaterials();

    // Geometry builders — keep Construct() readable. Each places its
    // detectors into the supplied world logical volume.
    void BuildEJ309Array(G4LogicalVolume* world);
    void BuildLaBr3Array(G4LogicalVolume* world);

    G4Material* fAir{nullptr};       // World fill (G4_AIR).
    G4Material* fU235{nullptr};      // 100% ²³⁵U disc (built by hand —
                                     // NIST only carries natural U).
    G4Material* fEJ309{nullptr};     // Organic liquid scintillator with
                                     // full per-particle MPT for PSD.
    G4Material* fLaBr3{nullptr};     // Inorganic crystal, single-component
                                     // scintillation (no PSD). Intrinsic
                                     // ¹³⁸La / ²²⁷Ac backgrounds NOT modeled.
    G4Material* fAluminum{nullptr};  // EJ-309 housings (1 mm shell).
};

#endif
