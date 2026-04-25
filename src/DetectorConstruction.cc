#include "DetectorConstruction.hh"

MyDetectorConstruction::MyDetectorConstruction()
    : fU235Material(nullptr),
      fWorldMaterial(nullptr)
{
    DefineMaterials();  // Build materials once at construction time
}

MyDetectorConstruction::~MyDetectorConstruction() {}

void MyDetectorConstruction::DefineMaterials()
{
    auto nist = G4NistManager::Instance();

    // World material — vacuum or air
    fWorldMaterial = nist->FindOrBuildMaterial("G4_AIR");

    // Scintillator — plastic scintillator from NIST database
    // fScintMaterial = nist->FindOrBuildMaterial("G4_PLASTIC_SC_VINYLTOLUENE");

    // Uranium-235 — built from scratch since NIST only has natural U
    G4Isotope* U235 = new G4Isotope("U235", 92, 235, 235.0439299*g/mole);
    G4Element* elU235 = new G4Element("EnrichedU235", "U", 1);
    elU235->AddIsotope(U235, 100.*perCent);

    fU235Material = new G4Material("Uranium235", 19.1*g/cm3, 1);
    fU235Material->AddElement(elU235, 100.*perCent);
}

G4VPhysicalVolume* MyDetectorConstruction::Construct()
{
    G4double worldHalf = 10.*cm;
    auto solidWorld = new G4Box("World", worldHalf, worldHalf, worldHalf);
    auto logicWorld = new G4LogicalVolume(solidWorld, fWorldMaterial, "World");
    auto physWorld  = new G4PVPlacement(nullptr, G4ThreeVector(),
                                        logicWorld, "World", nullptr, false, 0);

    auto solidTarget = new G4Box("UTarget", 0.5*um, 5.*mm, 5.*mm);
    auto logicTarget = new G4LogicalVolume(solidTarget, fU235Material, "UTarget");
    new G4PVPlacement(nullptr, G4ThreeVector(),
                      logicTarget, "UTarget", logicWorld, false, 0);

    return physWorld;
}