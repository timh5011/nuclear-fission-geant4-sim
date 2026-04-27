#include "DetectorConstruction.hh"

#include "G4Box.hh"
#include "G4Tubs.hh"
#include "G4LogicalVolume.hh"
#include "G4PVPlacement.hh"
#include "G4VPhysicalVolume.hh"
#include "G4ThreeVector.hh"
#include "G4RotationMatrix.hh"

#include "G4NistManager.hh"
#include "G4Material.hh"
#include "G4Element.hh"
#include "G4Isotope.hh"
#include "G4MaterialPropertiesTable.hh"
#include "G4IonisParamMat.hh"

#include "G4SystemOfUnits.hh"

#include <array>
#include <cmath>
#include <vector>

// =============================================================================
// Material / MPT citations
// =============================================================================
// EJ-309
//   - Eljen Technologies EJ-309 datasheet (Rev. 2017):
//       light yield 12,300 ph/MeV (electron-equivalent); peak emission 424 nm;
//       fast / slow decay components 3.5 ns / 32 ns; refractive index 1.57;
//       Birks kB ≈ 0.11 mm/MeV.
//   - Brooks F. D., NIM 4, 151 (1959):
//       particle-dependent fast-vs-slow component ratios — the physics behind
//       PSD. Heavier ionization (protons, ions) populates the slow triplet
//       states more efficiently than minimum-ionizing electrons.
//   - Enqvist A. et al., NIM A 715, 79 (2013):
//       proton light-output curve for EJ-309 (Birks-quenched fit).
//
// LaBr3(Ce)
//   - Saint-Gobain BrilLanCe-380 datasheet (Rev. 2014):
//       density 5.08 g/cm³; light yield 63,000 ph/MeV; peak emission 380 nm;
//       single decay constant 16 ns; refractive index 1.9.
//   - Moszynski M. et al., NIM A 568, 739 (2006):
//       Birks constant for LaBr₃(Ce) ~ 1.3 × 10⁻³ mm/MeV (very small —
//       inorganic scintillators have near-linear electron response).
// =============================================================================

MyDetectorConstruction::MyDetectorConstruction() {
    DefineMaterials();
}

MyDetectorConstruction::~MyDetectorConstruction() = default;

// -----------------------------------------------------------------------------
// DefineMaterials
// -----------------------------------------------------------------------------
void MyDetectorConstruction::DefineMaterials() {
    auto* nist = G4NistManager::Instance();

    // ---- Air (world fill) ---------------------------------------------------
    fAir = nist->FindOrBuildMaterial("G4_AIR");

    // ---- Aluminum (EJ-309 housings) ----------------------------------------
    fAluminum = nist->FindOrBuildMaterial("G4_Al");

    // ---- Pure ²³⁵U (foil) --------------------------------------------------
    // NIST supplies only natural uranium (~0.72 % ²³⁵U); we need 100 %.
    auto* U235 = new G4Isotope("U235", /*Z=*/92, /*A=*/235, 235.0439299*g/mole);
    auto* elU  = new G4Element("EnrichedU235", /*symbol=*/"U", /*nIsotopes=*/1);
    elU->AddIsotope(U235, 100.*perCent);
    fU235 = new G4Material("Uranium235", 19.1*g/cm3, 1);
    fU235->AddElement(elU, 100.*perCent);

    // =========================================================================
    // EJ-309 organic liquid scintillator
    // =========================================================================
    // Approximated as xylene (C8H10) at the EJ-309 datasheet density.
    // Composition by mass: H 9.5 %, C 90.5 %. Real EJ-309 contains
    // proprietary fluors / wavelength-shifters that are rolled into C —
    // the standard literature approximation.
    // =========================================================================
    auto* elH = nist->FindOrBuildElement("H");
    auto* elC = nist->FindOrBuildElement("C");
    fEJ309 = new G4Material("EJ309", 0.959*g/cm3, /*nElements=*/2);
    fEJ309->AddElement(elH,  9.5*perCent);
    fEJ309->AddElement(elC, 90.5*perCent);

    auto* mptEJ = new G4MaterialPropertiesTable();

    // Photon-energy grid — brackets the visible spectrum (1.5 eV ≈ 826 nm to
    // 4.5 eV ≈ 276 nm) and includes the 424 nm emission peak (≈ 2.92 eV).
    // Sampling is coarse — adequate while photon transport is unscored.
    const std::vector<G4double> phE = {
        1.5*eV, 2.0*eV, 2.5*eV, 2.92*eV, 3.5*eV, 4.0*eV, 4.5*eV
    };
    std::vector<G4double> rIndex(phE.size(), 1.57);
    // Bulk absorption length flat at 100 cm. Eljen does not publish a
    // wavelength-resolved attenuation curve, so flat is the standard
    // placeholder. Irrelevant until photon scoring is enabled.
    std::vector<G4double> absLen(phE.size(), 100.*cm);
    // Emission spectrum (un-normalized). The fast and slow components share
    // the same spectrum in EJ-309 to good approximation — only the time
    // profile differs, so we attach the same curve to COMPONENT1 and 2.
    std::vector<G4double> emissionEJ = {
        0.05, 0.30, 0.80, 1.00, 0.60, 0.20, 0.05
    };
    mptEJ->AddProperty("RINDEX",                  phE, rIndex);
    mptEJ->AddProperty("ABSLENGTH",               phE, absLen);
    mptEJ->AddProperty("SCINTILLATIONCOMPONENT1", phE, emissionEJ);
    mptEJ->AddProperty("SCINTILLATIONCOMPONENT2", phE, emissionEJ);

    // Default (electron-equivalent) yield. Per-particle yield curves below
    // override this when SetScintByParticleType(true) is set in main().
    mptEJ->AddConstProperty("SCINTILLATIONYIELD",          12300./MeV);
    mptEJ->AddConstProperty("RESOLUTIONSCALE",             1.0);
    mptEJ->AddConstProperty("SCINTILLATIONTIMECONSTANT1",  3.5*ns);
    mptEJ->AddConstProperty("SCINTILLATIONTIMECONSTANT2",  32.*ns);
    mptEJ->AddConstProperty("SCINTILLATIONYIELD1",         0.85);
    mptEJ->AddConstProperty("SCINTILLATIONYIELD2",         0.15);

    // Per-particle yield curves indexed by kinetic energy. Values are
    // CUMULATIVE photon yields (yield(KE) = total photons that would be
    // emitted slowing from KE to rest); G4Scintillation takes per-step
    // photon counts as yield(KE_in) − yield(KE_out). Without
    // SetScintByParticleType(true) in main(), these are silently ignored.

    // Electrons: linear (Birks effect negligible at MeV scale here).
    std::vector<G4double> eKE  = { 0., 0.001*MeV, 0.1*MeV, 1.0*MeV, 5.0*MeV, 10.0*MeV };
    std::vector<G4double> eY   = { 0., 12.3,      1230.,   12300.,  61500.,  123000. };
    mptEJ->AddProperty("ELECTRONSCINTILLATIONYIELD", eKE, eY);
    mptEJ->AddConstProperty("ELECTRONSCINTILLATIONYIELD1", 0.85);
    mptEJ->AddConstProperty("ELECTRONSCINTILLATIONYIELD2", 0.15);

    // Proton recoils — Birks-quenched per Enqvist 2013 fit. Non-linear curve
    // is the heart of fast-neutron detection in EJ-309 and the lever PSD
    // pulls on (more slow component than electrons of the same light yield).
    std::vector<G4double> pKE  = { 0., 0.5*MeV, 1.0*MeV, 2.0*MeV, 5.0*MeV, 10.*MeV, 20.*MeV };
    std::vector<G4double> pY   = { 0., 490.,    1970.,   6150.,   25800.,  68900.,  172000. };
    mptEJ->AddProperty("PROTONSCINTILLATIONYIELD", pKE, pY);
    mptEJ->AddConstProperty("PROTONSCINTILLATIONYIELD1", 0.55);
    mptEJ->AddConstProperty("PROTONSCINTILLATIONYIELD2", 0.45);

    // Heavy ions — fission fragments, very high dE/dx, heavily quenched.
    std::vector<G4double> ionKE = { 0., 1.*MeV, 5.*MeV, 10.*MeV, 50.*MeV, 100.*MeV };
    std::vector<G4double> ionY  = { 0., 200.,   1000.,  2000.,   10000.,  20000. };
    mptEJ->AddProperty("IONSCINTILLATIONYIELD", ionKE, ionY);
    mptEJ->AddConstProperty("IONSCINTILLATIONYIELD1", 0.40);
    mptEJ->AddConstProperty("IONSCINTILLATIONYIELD2", 0.60);

    // Alphas — similar quenching to ions.
    std::vector<G4double> aKE = { 0., 1.*MeV, 5.*MeV, 10.*MeV };
    std::vector<G4double> aY  = { 0., 1000.,  5000.,  10000. };
    mptEJ->AddProperty("ALPHASCINTILLATIONYIELD", aKE, aY);
    mptEJ->AddConstProperty("ALPHASCINTILLATIONYIELD1", 0.40);
    mptEJ->AddConstProperty("ALPHASCINTILLATIONYIELD2", 0.60);

    // Deuterons & tritons — can arise from rare n-induced reactions on H/C
    // (e.g., n + p → d). Geant4's per-particle scintillation requires curves
    // for every particle that deposits energy here; reuse the proton curve
    // as a reasonable first approximation (their light output ratios are
    // close to protons in EJ-309 when matched in dE/dx, per Pôzzi 2004).
    mptEJ->AddProperty("DEUTERONSCINTILLATIONYIELD", pKE, pY);
    mptEJ->AddConstProperty("DEUTERONSCINTILLATIONYIELD1", 0.55);
    mptEJ->AddConstProperty("DEUTERONSCINTILLATIONYIELD2", 0.45);
    mptEJ->AddProperty("TRITONSCINTILLATIONYIELD", pKE, pY);
    mptEJ->AddConstProperty("TRITONSCINTILLATIONYIELD1", 0.55);
    mptEJ->AddConstProperty("TRITONSCINTILLATIONYIELD2", 0.45);

    fEJ309->SetMaterialPropertiesTable(mptEJ);
    fEJ309->GetIonisation()->SetBirksConstant(0.11*mm/MeV);

    // =========================================================================
    // LaBr₃(Ce) inorganic scintillator
    // =========================================================================
    // High Z (effective ~46), high light yield, single 16 ns decay component.
    // No PSD capability — only one scintillation time constant.
    //
    // INTRINSIC BACKGROUNDS NOT MODELED:
    //   - ¹³⁸La (t½ = 1.05 × 10¹¹ yr, 0.089 % of natural La):
    //       continuous β + 1436 keV γ.
    //   - ²²⁷Ac contamination from raw material:
    //       α peaks at ~1.7–2.5 MeVee plus the full Ac-Th decay chain.
    // To add later: either mix natural-La and trace ²²⁷Ac isotopes into this
    // material and let G4RadioactiveDecayPhysics handle them, or fire events
    // from a G4GeneralParticleSource positioned inside the LaBr₃ logical
    // volume at the appropriate Bq/cm³ rate. See doc/architecture.md
    // "Intrinsic-background TODO" once it is added.
    // =========================================================================
    auto* elLa = nist->FindOrBuildElement("La");
    auto* elBr = nist->FindOrBuildElement("Br");
    auto* elCe = nist->FindOrBuildElement("Ce");
    fLaBr3 = new G4Material("LaBr3_Ce", 5.08*g/cm3, /*nElements=*/3);
    fLaBr3->AddElement(elLa, 34.85*perCent);
    fLaBr3->AddElement(elBr, 60.14*perCent);
    fLaBr3->AddElement(elCe,  5.01*perCent);

    auto* mptLaBr = new G4MaterialPropertiesTable();
    const std::vector<G4double> phEL = {
        2.5*eV, 3.0*eV, 3.26*eV, 3.5*eV, 4.0*eV
    };
    std::vector<G4double> rIndexL(phEL.size(), 1.9);
    std::vector<G4double> absLenL(phEL.size(), 50.*cm);
    std::vector<G4double> emissionLaBr = { 0.10, 0.50, 1.00, 0.50, 0.10 };
    mptLaBr->AddProperty("RINDEX",                  phEL, rIndexL);
    mptLaBr->AddProperty("ABSLENGTH",               phEL, absLenL);
    mptLaBr->AddProperty("SCINTILLATIONCOMPONENT1", phEL, emissionLaBr);
    mptLaBr->AddConstProperty("SCINTILLATIONYIELD",         63000./MeV);
    mptLaBr->AddConstProperty("RESOLUTIONSCALE",            1.0);
    mptLaBr->AddConstProperty("SCINTILLATIONTIMECONSTANT1", 16.*ns);
    mptLaBr->AddConstProperty("SCINTILLATIONYIELD1",        1.0);
    // Note: no COMPONENT2 / TIMECONSTANT2 / YIELD2 — single-component.

    // Per-particle yield curves are MANDATORY here (not just nice-to-have).
    // With G4OpticalParameters::SetScintByParticleType(true) globally on
    // (required for EJ-309 PSD), G4Scintillation::PostStepDoIt throws the
    // fatal Scint01 exception the moment any particle deposits energy in a
    // scintillator that lacks a per-particle yield curve. There is no
    // fallback to the global SCINTILLATIONYIELD constant in this mode.
    //
    // LaBr₃ has no PSD, so for now we use a single linear curve for every
    // particle type. Real LaBr₃ has α/β ≈ 0.3 (alpha light output is ~30 %
    // of electron-equivalent at the same energy) and similar quenching for
    // heavy ions — when photon scoring is enabled, these curves should be
    // refined per Moszynski 2006 / Saint-Gobain BrilLanCe documentation.
    std::vector<G4double> labrKE = {
        0., 0.001*MeV, 0.1*MeV, 1.0*MeV, 5.0*MeV, 10.*MeV
    };
    std::vector<G4double> labrY  = {
        0., 63.,       6300.,   63000.,  315000., 630000.
    };
    mptLaBr->AddProperty("ELECTRONSCINTILLATIONYIELD", labrKE, labrY);
    mptLaBr->AddConstProperty("ELECTRONSCINTILLATIONYIELD1", 1.0);
    mptLaBr->AddProperty("PROTONSCINTILLATIONYIELD", labrKE, labrY);
    mptLaBr->AddConstProperty("PROTONSCINTILLATIONYIELD1", 1.0);
    mptLaBr->AddProperty("IONSCINTILLATIONYIELD", labrKE, labrY);
    mptLaBr->AddConstProperty("IONSCINTILLATIONYIELD1", 1.0);
    mptLaBr->AddProperty("ALPHASCINTILLATIONYIELD", labrKE, labrY);
    mptLaBr->AddConstProperty("ALPHASCINTILLATIONYIELD1", 1.0);
    mptLaBr->AddProperty("DEUTERONSCINTILLATIONYIELD", labrKE, labrY);
    mptLaBr->AddConstProperty("DEUTERONSCINTILLATIONYIELD1", 1.0);
    mptLaBr->AddProperty("TRITONSCINTILLATIONYIELD", labrKE, labrY);
    mptLaBr->AddConstProperty("TRITONSCINTILLATIONYIELD1", 1.0);

    fLaBr3->SetMaterialPropertiesTable(mptLaBr);
    fLaBr3->GetIonisation()->SetBirksConstant(0.00131*mm/MeV);
}

// -----------------------------------------------------------------------------
// Construct — world, foil, both detector arrays
// -----------------------------------------------------------------------------
G4VPhysicalVolume* MyDetectorConstruction::Construct() {
    // World — 2 m × 2 m × 2 m air box (per design.md §4). Large enough to
    // contain the EJ-309 array (r=500 mm) and LaBr₃ array (r=300 mm) with
    // margin, even after the housings extend the EJ-309 footprint by 1 mm.
    auto* solidWorld = new G4Box("World", 1.0*m, 1.0*m, 1.0*m);
    auto* logicWorld = new G4LogicalVolume(solidWorld, fAir, "WorldLV");
    auto* physWorld  = new G4PVPlacement(nullptr, G4ThreeVector(),
                                          logicWorld, "World",
                                          nullptr, false, 0);

    // ²³⁵U foil — 20 mm dia × 0.5 µm thick disc, normal ∥ ẑ (per design.md §1.A).
    // The 0.5 µm thickness matches real electroplated/vapor-deposited fission
    // targets (CHAFF, SPIDER style; ~50–500 µg/cm²). Fragment ranges in U at
    // 19.1 g/cm³ are 6–8 µm, so roughly half of each fragment pair escapes
    // through one foil face with near-full kinetic energy — this is the
    // physical reason for the thinness and is what makes a fission target
    // visually distinct in the OGL viewer (tiny blue stubs at the foil).
    auto* solidFoil = new G4Tubs("Foil",
                                  /*rmin=*/0.,
                                  /*rmax=*/10.*mm,
                                  /*halfThick=*/0.25*um,
                                  /*startAngle=*/0.,
                                  /*endAngle=*/360.*deg);
    auto* logicFoil = new G4LogicalVolume(solidFoil, fU235, "FoilLV");
    new G4PVPlacement(nullptr, G4ThreeVector(0., 0., 0.),
                      logicFoil, "Foil", logicWorld, false, 0);

    BuildEJ309Array(logicWorld);
    BuildLaBr3Array(logicWorld);

    return physWorld;
}

// -----------------------------------------------------------------------------
// BuildEJ309Array — 8 cylinders + 1 mm Al housings, forward hemisphere
// -----------------------------------------------------------------------------
//
// Each EJ-309 cell is a 50 mm × 50 mm cylinder of liquid inside a 1 mm Al
// housing. The HOUSING is the placement-level volume in the world — its
// copy_no identifies the detector (0..7). The EJ-309 LIQUID is a single
// daughter logical volume placed once inside the (shared) housing logical;
// when the housing is copy-placed 8 times into the world, the liquid moves
// with it and inherits the same position+rotation. The liquid will be the
// sensitive volume in Phase B; the housing is non-sensitive (per the
// project's scope decision).
//
// Cylinder orientation: each cell is rotated so its local +ẑ (= cylinder
// axis) points radially outward from the foil. The front face is then
// normal to the line back to the origin — maximum projected area for a
// particle leaving the target.
//
// Geant4 rotation gotcha (READ THIS BEFORE EDITING): G4PVPlacement stores
// the rotation argument as the FRAME rotation (world → local for navigation),
// not as the OBJECT rotation (local → world for orientation). Confirm via
// G4VPhysicalVolume::GetObjectRotation() in the Geant4 source — it returns
// `frot->inverse()`, i.e. Geant4 inverts the stored rotation to obtain the
// object orientation. So if you mathematically build R such that
// R · ẑ_local = radial_world (the natural construction), you must invert it
// before handing it to G4PVPlacement, otherwise the cylinder ends up with
// its axis at R⁻¹·ẑ, which is the mirror image through the world ẑ-axis.
//
// Note on G4RotationMatrix lifetime: G4PVPlacement stores the rotation BY
// POINTER and does not deep-copy. We must `new` a fresh rotation per
// placement and not delete it (Geant4 does not free placement rotations
// on shutdown — the leak is a Geant4 idiom, not a bug).
// -----------------------------------------------------------------------------
void MyDetectorConstruction::BuildEJ309Array(G4LogicalVolume* world) {
    struct Spec { G4double theta; G4double phi; const char* id; };
    const std::array<Spec, 8> specs = {{
        { 30.*deg,   0.*deg, "EJ309-0" },
        { 30.*deg, 180.*deg, "EJ309-1" },
        { 60.*deg,  90.*deg, "EJ309-2" },
        { 60.*deg, 270.*deg, "EJ309-3" },
        { 90.*deg,   0.*deg, "EJ309-4" },
        { 90.*deg, 180.*deg, "EJ309-5" },
        {120.*deg,  90.*deg, "EJ309-6" },
        {120.*deg, 270.*deg, "EJ309-7" },
    }};

    const G4double R         = 500.*mm;       // distance from foil
    const G4double rCyl      = 25.*mm;        // EJ-309 radius (50 mm dia)
    const G4double hCyl      = 25.*mm;        // EJ-309 half-length (50 mm)
    const G4double tHousing  = 1.*mm;         // Al wall thickness

    // Outer (housing) solid: 1 mm Al all around, side wall and end caps.
    auto* solidHouse = new G4Tubs("EJ309HouseSolid",
                                   /*rmin=*/0.,
                                   /*rmax=*/rCyl + tHousing,
                                   /*halfZ=*/hCyl + tHousing,
                                   0., 360.*deg);
    auto* logicHouse = new G4LogicalVolume(solidHouse, fAluminum, "EJ309HouseLV");

    // Inner (liquid) solid placed once at center inside the housing logical.
    auto* solidEJ = new G4Tubs("EJ309LiquidSolid",
                                /*rmin=*/0.,
                                /*rmax=*/rCyl,
                                /*halfZ=*/hCyl,
                                0., 360.*deg);
    auto* logicEJ = new G4LogicalVolume(solidEJ, fEJ309, "EJ309LV");
    new G4PVPlacement(nullptr, G4ThreeVector(),
                      logicEJ, "EJ309Liquid",
                      logicHouse, /*pMany=*/false, /*copy=*/0);

    // Place 8 housings (each carrying a daughter liquid placement) at the
    // tabulated spherical coordinates with copy_no = i. The Phase B
    // ScintillatorSD will look up `i` against the 8-string detector_id table.
    for (size_t i = 0; i < specs.size(); ++i) {
        const auto& s = specs[i];
        const G4double sin_t = std::sin(s.theta);
        const G4double cos_t = std::cos(s.theta);
        const G4double sin_p = std::sin(s.phi);
        const G4double cos_p = std::cos(s.phi);
        const G4ThreeVector center(R * sin_t * cos_p,
                                    R * sin_t * sin_p,
                                    R * cos_t);

        // Build the OBJECT rotation R such that R·ẑ_local = radial_world,
        // using Rodrigues with axis = ẑ × radial and angle = ∠(ẑ, radial).
        // For our θ ∈ {30°, 60°, 90°, 120°} the cross product is non-degenerate.
        // Then invert — see the Geant4 frame-vs-object rotation note above.
        const G4ThreeVector zAxis(0., 0., 1.);
        const G4ThreeVector radial  = center.unit();
        const G4ThreeVector axisRaw = zAxis.cross(radial);
        auto* rot = new G4RotationMatrix();
        if (axisRaw.mag() > 1e-9) {
            const G4double angle = std::acos(zAxis.dot(radial));
            rot->rotate(angle, axisRaw.unit());
        }
        rot->invert();   // pass the FRAME rotation, not the object rotation

        new G4PVPlacement(rot, center,
                          logicHouse, s.id,
                          world, /*pMany=*/false,
                          /*copy=*/static_cast<G4int>(i));
    }
}

// -----------------------------------------------------------------------------
// BuildLaBr3Array — 2 bare LaBr₃(Ce) cylinders at backward angles
// -----------------------------------------------------------------------------
//
// LaBr₃ cells are placed directly in the world with no housing (per
// design.md §3). Smaller volume (38 mm × 38 mm) and closer to the target
// (r=300 mm) than the EJ-309 array — the higher Z gives much larger
// photopeak efficiency per unit volume, and the backward placement
// minimizes neutron exposure. Both detectors live at θ=135°.
//
// The LaBr₃ logical volume's copy_no directly identifies the detector
// (0..1) since there's no housing parent — Phase B's ScintillatorSD
// looks it up against the 2-string detector_id table.
// -----------------------------------------------------------------------------
void MyDetectorConstruction::BuildLaBr3Array(G4LogicalVolume* world) {
    struct Spec { G4double theta; G4double phi; const char* id; };
    const std::array<Spec, 2> specs = {{
        {135.*deg,  45.*deg, "LaBr3-0"},
        {135.*deg, 225.*deg, "LaBr3-1"},
    }};

    const G4double R    = 300.*mm;
    const G4double rCyl = 19.*mm;       // 38 mm dia
    const G4double hCyl = 19.*mm;       // 38 mm length

    auto* solidLaBr = new G4Tubs("LaBr3Solid",
                                  /*rmin=*/0.,
                                  /*rmax=*/rCyl,
                                  /*halfZ=*/hCyl,
                                  0., 360.*deg);
    auto* logicLaBr = new G4LogicalVolume(solidLaBr, fLaBr3, "LaBr3LV");

    for (size_t i = 0; i < specs.size(); ++i) {
        const auto& s = specs[i];
        const G4double sin_t = std::sin(s.theta);
        const G4double cos_t = std::cos(s.theta);
        const G4double sin_p = std::sin(s.phi);
        const G4double cos_p = std::cos(s.phi);
        const G4ThreeVector center(R * sin_t * cos_p,
                                    R * sin_t * sin_p,
                                    R * cos_t);

        // Same rotation pattern as EJ-309 — see the comment block above
        // BuildEJ309Array for the Geant4 frame-vs-object rotation gotcha.
        const G4ThreeVector zAxis(0., 0., 1.);
        const G4ThreeVector radial  = center.unit();
        const G4ThreeVector axisRaw = zAxis.cross(radial);
        auto* rot = new G4RotationMatrix();
        if (axisRaw.mag() > 1e-9) {
            const G4double angle = std::acos(zAxis.dot(radial));
            rot->rotate(angle, axisRaw.unit());
        }
        rot->invert();

        new G4PVPlacement(rot, center,
                          logicLaBr, s.id,
                          world, /*pMany=*/false,
                          /*copy=*/static_cast<G4int>(i));
    }
}
