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

    G4ThreeVector pos(0, 0, -1 * cm); // Defines particle initial position
    G4ThreeVector mom(0.,0.,1.);// Defines particle initial momentum direction

    fParticleGun->SetParticlePosition(pos);
    fParticleGun->SetParticleMomentumDirection(mom);
    fParticleGun->SetParticleEnergy(0.025*eV);
    fParticleGun->SetParticleDefinition(particle);

    fParticleGun->GeneratePrimaryVertex(anEvent);
}