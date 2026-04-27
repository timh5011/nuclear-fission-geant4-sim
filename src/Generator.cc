#include "Generator.hh"

MyPrimaryGenerator::MyPrimaryGenerator() {
    fParticleGun = new G4ParticleGun(1);
}

MyPrimaryGenerator::~MyPrimaryGenerator() {
    delete fParticleGun;
}

void MyPrimaryGenerator::GeneratePrimaries(G4Event *anEvent){
    G4ParticleTable *particleTable = G4ParticleTable::GetParticleTable();
    G4ParticleDefinition *particle = particleTable->FindParticle("neutron");

    // Per design.md §5: pencil beam of thermal neutrons from z = -100 mm
    // along +ẑ, normal incidence on the foil at the origin.
    // The flight path is intentionally long so the beam can be replaced later
    // with a thermalized source distribution without re-arranging geometry.
    // Thermal speed (2200 m/s) over 100 mm is ~45 µs of flight time — fine for
    // the prompt-only window since we don't register G4NeutronTrackingCut.
    G4ThreeVector pos(0., 0., -100.*mm);
    G4ThreeVector mom(0., 0., 1.);

    fParticleGun->SetParticlePosition(pos);
    fParticleGun->SetParticleMomentumDirection(mom);
    fParticleGun->SetParticleEnergy(0.025*eV);
    fParticleGun->SetParticleDefinition(particle);

    fParticleGun->GeneratePrimaryVertex(anEvent);
}
