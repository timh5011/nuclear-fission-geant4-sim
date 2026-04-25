#ifndef PHYSICSLIST_HH
#define PHYSICSLIST_HH

#include "G4VModularPhysicsList.hh"

// ============================================================
// ELECTROMAGNETIC PHYSICS
// Handles all charged particle interactions in both the
// uranium target and the scintillator detector:
//   - Photoelectric effect, Compton scattering, pair production
//   - Ionization and energy loss of electrons, positrons,
//     betas, fission fragments, recoil protons
//   - Bremsstrahlung, multiple scattering
//   - Option4 is the most accurate EM configuration
// ============================================================
#include "G4EmStandardPhysics_option4.hh"

// ============================================================
// OPTICAL PHYSICS
// Scintillation, Cherenkov, optical absorption, Rayleigh
// scattering, boundary processes (reflection/refraction).
// Only active if you define optical material properties
// (SCINTILLATIONYIELD, RINDEX, ABSLENGTH, etc.) on your
// scintillator logical volume's material properties table.
// Without those properties, this is effectively a no-op.
// ============================================================
#include "G4OpticalPhysics.hh"

// ============================================================
// HADRONIC PHYSICS — HIGH PRECISION NEUTRON TRANSPORT
// Uses the G4NDL evaluated data library (ENDF/B-VII) for
// neutrons below 20 MeV. Registers four HP processes:
//   - Elastic scattering (important for neutron detection
//     via proton recoil in plastic scintillator)
//   - Inelastic scattering
//   - Radiative capture (n + 235U -> 236U + gammas)
//   - Fission (n + 235U -> fragments + neutrons + gammas)
// Above 20 MeV, falls back to Binary Cascade (BIC) model.
// ============================================================
#include "G4HadronPhysicsQGSP_BIC_HP.hh"
#include "G4HadronElasticPhysicsHP.hh"

// ============================================================
// HADRONIC PHYSICS — SUPPLEMENTARY
// EmExtra: photo-nuclear and electro-nuclear reactions
//          (gamma + nucleus -> hadrons). Relevant if high-
//          energy gammas interact with your uranium or detector.
// Ion:     hadronic interactions of ions (fission fragments
//          undergoing nuclear reactions as they traverse matter)
// IonElastic: elastic scattering of ions
// Stopping: capture-at-rest processes (e.g. muon capture,
//           pion capture — included for completeness)
// ============================================================
#include "G4EmExtraPhysics.hh"
#include "G4IonPhysics.hh"
#include "G4IonElasticPhysics.hh"
#include "G4StoppingPhysics.hh"

// ============================================================
// DECAY PHYSICS
// G4DecayPhysics: decay of unstable particles (muons,
//   pions, kaons, etc.)
// G4RadioactiveDecayPhysics: alpha, beta+, beta-, EC decay
//   of radioactive nuclei. This is what makes your fission
//   fragments undergo their beta decay chains, producing
//   delayed betas, delayed gammas, and delayed neutrons.
//   Uses ENSDF evaluated nuclear data.
// ============================================================
#include "G4DecayPhysics.hh"
#include "G4RadioactiveDecayPhysics.hh"

class MyPhysicsList : public G4VModularPhysicsList
{
public:
    MyPhysicsList()
    {
        SetVerboseLevel(1);

        // EM physics — option4 for best accuracy
        RegisterPhysics(new G4EmStandardPhysics_option4());

        // Optical physics — scintillation, Cherenkov, boundary processes
        // Configure after construction if needed (see below)
        auto opticalPhysics = new G4OpticalPhysics();
        // Enable scintillation with Birks saturation (critical for PSD)
        // opticalPhysics->SetScintillationByParticleType(true);  // removed in Geant4 v11
        RegisterPhysics(opticalPhysics);

        // Hadronic physics — HP neutron transport + BIC cascade
        RegisterPhysics(new G4HadronPhysicsQGSP_BIC_HP());
        RegisterPhysics(new G4HadronElasticPhysicsHP());

        // Supplementary hadronic
        RegisterPhysics(new G4EmExtraPhysics());
        RegisterPhysics(new G4IonPhysics());
        RegisterPhysics(new G4IonElasticPhysics());
        RegisterPhysics(new G4StoppingPhysics());

        // Decay
        RegisterPhysics(new G4DecayPhysics());
        RegisterPhysics(new G4RadioactiveDecayPhysics());
    }

    ~MyPhysicsList() override = default;
};

#endif