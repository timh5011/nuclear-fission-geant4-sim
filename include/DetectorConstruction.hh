#ifndef DETECTORCONSTRUCTION_HH
#define DETECTORCONSTRUCTION_HH

#include "G4VUserDetectorConstruction.hh"
#include "G4VPhysicalVolume.hh"
#include "G4LogicalVolume.hh"
#include "G4Box.hh"
#include "G4PVPlacement.hh"
#include "G4ThreeVector.hh"
#include "G4OpticalSurface.hh"
#include "G4LogicalSkinSurface.hh"
#include "G4Tubs.hh"

// For U-235 material definition:
#include "G4NistManager.hh"
#include "G4Material.hh"
#include "G4Element.hh"
#include "G4Isotope.hh"
#include "G4SystemOfUnits.hh"

#include "Detector.hh"

class MyDetectorConstruction : public G4VUserDetectorConstruction {
    public:
        MyDetectorConstruction();
        ~MyDetectorConstruction();

        virtual G4VPhysicalVolume *Construct();

    private:
        void DefineMaterials();

        // Material pointers — stored so Construct() can use them
        G4Material* fU235Material;
        // G4Material* fScintMaterial;
        G4Material* fWorldMaterial;
};

#endif