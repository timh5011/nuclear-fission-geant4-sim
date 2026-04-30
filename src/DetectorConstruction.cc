#include "DetectorConstruction.hh"

#include "ScintillatorSD.hh"

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
#include "G4OpticalSurface.hh"
#include "G4LogicalSkinSurface.hh"
#include "G4SDManager.hh"

#include "G4SystemOfUnits.hh"

#include <array>
#include <cmath>
#include <string>
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

    // =========================================================================
    // Tyvek / PTFE-equivalent diffuse reflective wrap
    // =========================================================================
    // Models a high-reflectivity diffuse white wrapper (Tyvek, PTFE/Teflon,
    // Spectralon, MgO powder) covering every face of every scintillator. Real
    // EJ-309 cells have such a wrap inside the Al housing; real LaBr₃(Ce)
    // crystals are sold pre-wrapped inside their hermetic can. A single shared
    // G4OpticalSurface stands in for both — registered as a G4LogicalSkinSurface
    // on each scintillator's logical volume in BuildEJ309Array / BuildLaBr3Array.
    //
    // unified model + groundfrontpainted finish:
    //   • A photon hitting the boundary either reflects with probability
    //     REFLECTIVITY or is absorbed with probability (1 − REFLECTIVITY).
    //   • "ground"      → reflection is purely Lambertian (cos θ).
    //   • "frontpainted" → no refraction into the second medium; the paint
    //     layer is what the photon interacts with, so the neighbor's RINDEX
    //     does not matter (this is why the bare LaBr₃/air boundary still
    //     behaves like a wrapped crystal).
    //
    // Reflectivity 0.98 is the standard literature value for clean PTFE wrap
    // across the visible band — see Janecek & Moses, IEEE TNS 55 (2008) 2432.
    // =========================================================================
    fTyvekWrap = new G4OpticalSurface("TyvekWrap");
    fTyvekWrap->SetModel(unified);
    fTyvekWrap->SetType(dielectric_dielectric);
    fTyvekWrap->SetFinish(groundfrontpainted);

    auto* mptTyvek = new G4MaterialPropertiesTable();
    // Two-point flat grid is sufficient — REFLECTIVITY is wavelength-independent
    // to the precision PTFE/Tyvek datasheets quote across 350–700 nm, which
    // brackets both EJ-309 (424 nm) and LaBr₃ (380 nm) emission peaks.
    const std::vector<G4double> phETyvek = { 1.5*eV, 4.5*eV };
    const std::vector<G4double> reflTyvek(phETyvek.size(), 0.98);
    const std::vector<G4double> effTyvek (phETyvek.size(), 0.0);
    mptTyvek->AddProperty("REFLECTIVITY", phETyvek, reflTyvek);
    mptTyvek->AddProperty("EFFICIENCY",   phETyvek, effTyvek);
    fTyvekWrap->SetMaterialPropertiesTable(mptTyvek);
}

// -----------------------------------------------------------------------------
// Construct — world, foil, both detector arrays
// -----------------------------------------------------------------------------
G4VPhysicalVolume* MyDetectorConstruction::Construct() {
    // World — 6 m × 6 m × 6 m air box (per design.md §4). Large enough to
    // contain the EJ-309 array (r=500 mm) and LaBr₃ array (r=300 mm) with
    // margin, even after the housings extend the EJ-309 footprint by 1 mm.
    auto* solidWorld = new G4Box("World", 3.0*m, 3.0*m, 3.0*m);
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
// BuildEJ309Array — 48 cylinders + 1 mm Al housings, forward hemisphere
// -----------------------------------------------------------------------------
//
// Each EJ-309 cell is an 80 mm dia × 50 mm length cylinder of liquid inside a 1 mm Al
// housing. The HOUSING is the placement-level volume in the world — its
// copy_no identifies the detector (0..47). The EJ-309 LIQUID is a single
// daughter logical volume placed once inside the (shared) housing logical;
// when the housing is copy-placed 48 times into the world, the liquid moves
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
    // 48-cell densified array. Two interchangeable layouts are kept side by
    // side; uncomment exactly ONE block. Both produce a 48-element `specs`
    // array consumed by the (shared) placement loop below; ConstructSDandField
    // builds a 48-string EJ309-{0..47} id table that pairs with the active
    // block's index ordering. Polar band θ ∈ [20°, 130°] in both cases.
    //
    // Cell housing radius is 41 mm at r = 500 mm, so the closest-pair
    // clearance threshold is 82 mm linear (5.94° great-circle). Both layouts
    // pass with substantial margin (see comments in each block).
    struct Spec { G4double theta; G4double phi; };
    constexpr int nCells = 48;
    std::array<Spec, nCells> specs;

    // =========================================================================
    // LAYOUT OPTION A — 6 polar rings × 8 azimuthal positions  [ACTIVE]
    // =========================================================================
    // Six rings at θ = 20°, 42°, 64°, 86°, 108°, 130° (uniform 22° steps);
    // eight φ slots per ring, staggered 22.5° on alternate rings. Detector IDs
    // are ring-major: ring r, slot k → "EJ309-{8r+k}". Closest pair: adjacent
    // ring, Δφ = 22.5° staggered → ~210 mm great-circle separation.
    //
    // To activate: uncomment this block AND comment out the Fibonacci block
    // below. Only one block must run — both write to the same `specs`.
    // =========================================================================
    /*
    */
    {
        constexpr int nRings = 6;
        constexpr int nPhi   = 8;
        static_assert(nRings * nPhi == nCells, "option A cell count mismatch");
        const G4double thetaMin = 20.*deg;
        const G4double thetaMax = 130.*deg;
        const G4double dTheta   = (thetaMax - thetaMin) / (nRings - 1);
        const G4double dPhi     = 360.*deg / nPhi;
        for (int r = 0; r < nRings; ++r) {
            const G4double theta = thetaMin + r * dTheta;
            const G4double phi0  = (r % 2 == 0) ? 0. : 0.5 * dPhi;
            for (int k = 0; k < nPhi; ++k) {
                specs[r * nPhi + k] = { theta, phi0 + k * dPhi };
            }
        }
    }
    // =========================== END OF OPTION A ===========================

    // =========================================================================
    // LAYOUT OPTION C — Fibonacci (golden-angle) spiral on θ ∈ [20°, 130°]  [INACTIVE]
    // =========================================================================
    // 48 points distributed uniformly in solid angle across the band by
    // stepping through equal Δ(cos θ) intervals while advancing φ by the
    // golden angle (137.508°). Loses ring symmetry — every cell has a unique
    // (θ, φ) — but gives the smallest nearest-neighbor variance for a given
    // count. ID i is the i-th sample of the spiral, ordered in increasing θ
    // (decreasing cos θ). Closest-pair separation typical ~200 mm.
    //
    // The (i + 0.5)/N midpoint offset puts the first/last samples one half-
    // step inside the band edges (avoids placing a cell exactly on θ_min /
    // θ_max). `CLHEP::pi` / `CLHEP::twopi` are pulled in by G4SystemOfUnits.h
    // (the unqualified `pi` macro is not exposed by Geant4 v11 headers).
    // =========================================================================
    /*
    {
        const G4double thetaMin    = 20.*deg;
        const G4double thetaMax    = 130.*deg;
        const G4double cosA        = std::cos(thetaMin);   // larger cosθ
        const G4double cosB        = std::cos(thetaMax);   // smaller cosθ
        const G4double goldenAngle = CLHEP::pi * (3. - std::sqrt(5.));   // ≈ 137.508°
        for (int i = 0; i < nCells; ++i) {
            const G4double cosT  = cosA + (i + 0.5) / nCells * (cosB - cosA);
            const G4double theta = std::acos(cosT);
            const G4double phi   = std::fmod(i * goldenAngle, CLHEP::twopi);
            specs[i] = { theta, phi };
        }
    }
    */

    // =========================== END OF OPTION C ===========================

    const G4double R         = 500.*mm;       // distance from foil
    const G4double rCyl      = 40.*mm;        // EJ-309 radius (80 mm dia)
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

    // Tyvek/PTFE-equivalent diffuse reflective skin on every face of the
    // liquid. Skin (vs. border) is correct here: the wrap is uniform on all
    // faces of every cell, so one registration covers all 48 placements
    // automatically. When a PMT/SiPM coupling face is added later, override
    // just that one face with a G4LogicalBorderSurface — the skin remains
    // the default for every other boundary.
    new G4LogicalSkinSurface("EJ309TyvekSkin", logicEJ, fTyvekWrap);

    // Place all 48 housings (each carrying a daughter liquid placement) at
    // the spherical coordinates produced by the active layout block above,
    // with copy_no = i. The placement name is synthesized from the index
    // ("EJ309-i"); ScintillatorSD looks up `i` against the 48-string
    // detector_id table built in ConstructSDandField — ordering matches
    // whichever layout block is active.
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

        const G4String name = "EJ309-" + std::to_string(i);
        new G4PVPlacement(rot, center,
                          logicHouse, name,
                          world, /*pMany=*/false,
                          /*copy=*/static_cast<G4int>(i));
    }
}

// -----------------------------------------------------------------------------
// BuildLaBr3Array — 2 bare LaBr₃(Ce) cylinders at backward angles
// -----------------------------------------------------------------------------
//
// LaBr₃ cells are placed directly in the world with no housing (per
// design.md §3). 120 mm dia × 38 mm length, closer to the target
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
    const G4double rCyl = 60.*mm;       // 120 mm dia
    const G4double hCyl = 19.*mm;       // 38 mm length

    auto* solidLaBr = new G4Tubs("LaBr3Solid",
                                  /*rmin=*/0.,
                                  /*rmax=*/rCyl,
                                  /*halfZ=*/hCyl,
                                  0., 360.*deg);
    auto* logicLaBr = new G4LogicalVolume(solidLaBr, fLaBr3, "LaBr3LV");

    // Same Tyvek/PTFE skin as EJ-309 — real LaBr₃(Ce) crystals are shipped
    // pre-wrapped (typically MgO powder or PTFE) inside their hermetic Al
    // housing. The bare crystal in this sim has no housing, so the skin
    // surface stands in directly for "the wrapped crystal as delivered."
    new G4LogicalSkinSurface("LaBr3TyvekSkin", logicLaBr, fTyvekWrap);

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

// -----------------------------------------------------------------------------
// ConstructSDandField — attach the two ScintillatorSD instances
// -----------------------------------------------------------------------------
//
// Two SDs total, one per scintillator material:
//
//   • EJ309SD attached to the EJ-309 LIQUID logical (EJ309LV). The liquid is
//     placed inside EJ309HouseLV, which is the volume copy-placed 48× into
//     the world. So the per-detector copy_no lives one level UP the
//     touchable history → copyNoDepth = 1.
//
//   • LaBr3SD attached to LaBr3LV directly. The crystal is placed bare in
//     the world with copy_no 0..1 → copyNoDepth = 0.
//
// HitWriter* is injected per-run by MyRunAction::BeginOfRunAction (writers
// don't exist at construction time — they're built once the run starts and
// the timestamped output directory is known). RunAction looks up the SDs
// by name via G4SDManager::FindSensitiveDetector, so the names registered
// here ("EJ309SD"/"LaBr3SD") must match the strings in RunAction.
// -----------------------------------------------------------------------------
void MyDetectorConstruction::ConstructSDandField() {
    auto* sdMan = G4SDManager::GetSDMpointer();

    // Detector-id table for the 48-cell EJ-309 array. Indexing matches the
    // active layout block in BuildEJ309Array — ring-major (ring r, slot k →
    // 8r+k) for option A, spiral-sample order (i = 0..47, increasing θ) for
    // option C/Fibonacci.
    std::vector<G4String> ej309Ids;
    ej309Ids.reserve(48);
    for (int i = 0; i < 48; ++i) {
        ej309Ids.emplace_back("EJ309-" + std::to_string(i));
    }
    auto* ej309SD = new ScintillatorSD(
        "EJ309SD",
        ej309Ids,
        /*copyNoDepth=*/1);
    sdMan->AddNewDetector(ej309SD);
    SetSensitiveDetector("EJ309LV", ej309SD);

    auto* laBr3SD = new ScintillatorSD(
        "LaBr3SD",
        std::vector<G4String>{ "LaBr3-0", "LaBr3-1" },
        /*copyNoDepth=*/0);
    sdMan->AddNewDetector(laBr3SD);
    SetSensitiveDetector("LaBr3LV", laBr3SD);
}
