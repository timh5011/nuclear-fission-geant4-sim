# Design Specification: Thermal Neutron-Induced ²³⁵U Fission Detector

## Overview

GEANT4 v11.4.0 simulation of thermal neutron-induced fission of ²³⁵U, with a
scintillator-based detector system designed to reconstruct the prompt and delayed
radiation output of the fission event. The system consists of three subsystems:

1. **Fission trigger** — thin plastic scintillator in contact with the ²³⁵U foil
2. **Organic scintillator array** — hemisphere of EJ-309 liquid scintillators for
   neutron/gamma detection with pulse shape discrimination (PSD)
3. **Inorganic scintillator detectors** — LaBr₃(Ce) crystals at backward angles
   for gamma-ray spectroscopy

**This document describes the full designed system.** Not every component below
is yet built in the simulation — see the *Implementation Status* section at the
end for the per-component build state. Sections that describe deferred
components are kept in place so the design intent is preserved for future work.

---

## Coordinate System and Conventions

- Origin at the center of the ²³⁵U foil
- Neutron beam along the +z axis
- Foil normal along ±z
- Organic array covers the forward hemisphere (+z)
- LaBr₃ detectors at backward angles (−z hemisphere)
- All angles measured from +z (beam axis): θ = 0° is forward, θ = 180° is backward

---

## 1. Fission Target Assembly

### ²³⁵U Foil

| Parameter | Value | Notes |
|---|---|---|
| Material | Pure ²³⁵U (100% enriched) | — |
| Geometry | Disc (G4Tubs) | — |
| Diameter | 20 mm | — |
| Thickness | 0.5 μm | Thin enough for fission fragments to escape |
| Density | 19.1 g/cm³ | Metallic uranium |
| Position | Origin, foil normal along z-axis | — |

**Rationale:** 0.5 μm thickness matches real electroplated/vacuum-deposited
fission targets (CHAFF, SPIDER style, ~50–500 µg/cm²). Roughly half the
fission fragments escape via each foil face with near-full energy — minimal
in-foil dE/dx, so the trigger pulse-height spectrum reflects fragment
kinematics, not foil geometry. 20 mm diameter provides a reasonable target
area for the neutron beam while keeping the target small relative to detector
distances.

### Trigger Scintillator

> **Implementation status: deferred.** Not placed in Phase A. The simulation
> recovers fission timing directly from the Geant4 vertex (no physical t=0
> reference is needed in simulation), so the trigger is only required when
> modelling a real-experiment DAQ. The spec below is preserved as the
> intended design for when this is added.

| Parameter | Value | Notes |
|---|---|---|
| Material | EJ-212 plastic scintillator | NIST: G4_PLASTIC_SC_VINYLTOLUENE (approximate) |
| Geometry | Disc (G4Tubs) | — |
| Diameter | 25 mm | Slightly larger than foil to catch edge fragments |
| Thickness | 500 μm | Stops all fission fragments; transparent to most gammas |
| Position | Immediately behind foil at z = −0.5 mm | In contact with or very close to foil |
| Sensitive detector | Yes | Score energy deposition, apply ~3 MeVee threshold to select fragments |

**Detection principle:** Fission fragments deposit ~80–100 MeV (quenched to
~5–15 MeVee via Birks' law) in the first ~20 μm. Alpha particles from ²³⁵U
decay deposit ~4.7 MeV (quenched to ~1–2 MeVee). Gammas deposit < 0.5 MeVee.
A simple pulse height threshold cleanly selects fission events and provides
the t = 0 timing reference.

**Birks' constant for EJ-212:** kB ≈ 0.126 mm/MeV (typical PVT-based plastic).

### Backing / Support (Optional)

> **Implementation status: deferred.** Not placed in Phase A.

| Parameter | Value | Notes |
|---|---|---|
| Material | Aluminum | — |
| Geometry | Disc (G4Tubs) | — |
| Diameter | 30 mm | — |
| Thickness | 50 μm | Thin enough to be mostly transparent to gammas and neutrons |
| Position | z = −1.0 mm (behind trigger scintillator) | — |

**Note:** The backing provides mechanical support for the foil + trigger
assembly.

---

## 2. Organic Scintillator Array (Forward Hemisphere)

### Material: EJ-309 Liquid Scintillator

| Property | Value |
|---|---|
| Composition | Organic liquid (xylene-based), H/C ratio ~1.25 |
| Density | 0.959 g/cm³ |
| Light output | ~12,300 photons/MeV (electrons) |
| Peak emission wavelength | 424 nm |
| Decay time (fast component) | 3.5 ns |
| Decay time (slow component) | 32 ns |
| Birks' constant | kB ≈ 0.11 mm/MeV |
| H atoms/cm³ | ~5.43 × 10²² |

**Composition:**
- C: 84.2% by mass
- H: 9.5% by mass
- Remainder: proprietary solvent/fluor; approximated as pure xylene
  (C₈H₁₀) at 0.959 g/cm³.

Optical properties (RINDEX, SCINTILLATIONYIELD, FASTTIMECONSTANT,
SLOWTIMECONSTANT, YIELDRATIO) are specified on the material properties
table for optical photon transport.

**Material variant:** Stilbene (trans-C₁₄H₁₂, density 1.16 g/cm³) is a
candidate substitute for EJ-309 with stronger PSD response, particularly
for low-energy delayed neutrons. Stilbene geometry is the same solid
cylinder; only material definition and optical properties differ.
*Implementation status: deferred — only EJ-309 is built in Phase A.*

### Per-Particle Scintillation Parameters (Phase A implementation)

PSD requires the fast/slow scintillation ratio to depend on particle type
(see `doc/theory.md` §6 for the underlying physics — singlet vs triplet
state population as a function of dE/dx). The table below lists the values
attached to the EJ-309 `G4MaterialPropertiesTable` in
`src/DetectorConstruction.cc::DefineMaterials()`. Per-particle yield curves
are activated by `G4OpticalParameters::Instance()->SetScintByParticleType(true)`
in `nuclear-fission.cc` before `runManager->Initialize()`.

| Particle | YIELD1 (fast frac.) | YIELD2 (slow frac.) | Yield curve type |
|---|---|---|---|
| Electron | 0.85 | 0.15 | Linear, 12,300 ph/MeV |
| Proton (recoil) | 0.55 | 0.45 | Birks-quenched (Enqvist 2013 fit) |
| Heavy ion (fission frag) | 0.40 | 0.60 | Heavily quenched |
| Alpha | 0.40 | 0.60 | Same as ion |
| Deuteron / triton | 0.55 / 0.45 | (proton curve) | Reused proton curve |

The slow-fraction increase from 0.15 (electrons) to 0.45 (protons) and 0.60
(ions) is the lever PSD pulls on. Citations: Brooks F. D., NIM 4, 151 (1959);
Enqvist A. et al., NIM A 715, 79 (2013); Pôzzi S. A. et al. (EJ-301/NE-213
series). See `doc/theory.md` §6 for full derivation.

### Detector Geometry

| Parameter | Value | Notes |
|---|---|---|
| Shape | Right circular cylinder (G4Tubs) | Standard detector form factor |
| Diameter | 50 mm (2 inches) | — |
| Length | 50 mm (2 inches) | — |
| Housing | 1 mm aluminum shell | Structural / light-tight outer can; **not** an optical reflector. |

The aluminum housing is a structural / environmental shell only — it contains
the EJ-309 liquid, blocks ambient room light from reaching the (future) PMT
photocathode, and provides a 1 mm passive absorber/scatterer in front of the
active volume. It is **not** a scintillation-light reflector. Light containment
is handled by a separate diffuse reflective wrap (next subsection).

### Reflective Wrap

| Parameter | Value | Notes |
|---|---|---|
| Function | Diffuse reflective wrap on every face of the liquid | Tyvek / PTFE / Spectralon / MgO analog |
| Geant4 model | `unified` | Required for `groundfrontpainted` finish |
| Surface type | `dielectric_dielectric` | Paint-layer model; neighbor's `RINDEX` irrelevant |
| Finish | `groundfrontpainted` | Lambertian (cos θ) reflection, no transmission |
| Reflectivity | 0.98, flat 1.5–4.5 eV | Janecek & Moses, IEEE TNS 55, 2432 (2008) |
| Geant4 attachment | `G4LogicalSkinSurface` on `EJ309LV` | One registration covers all 8 placements |

**Rationale.** Real EJ-309 cells carry a high-reflectivity diffuse wrap
(Tyvek, PTFE/Teflon, Spectralon, or VM2000 specular film) inside the Al
housing. The wrap, not the can, is what contains scintillation light; the can
is structure and light-tightness only. Modelling the wrap as a
`groundfrontpainted` skin surface bypasses the need to define optical
properties on the aluminum (which has none) and matches typical lab-frame
photometric measurements of clean PTFE wraps to within ~1–2 %. Because the
finish is "front painted," the photon never enters the second medium — the
neighbor (Al, air, anything) is irrelevant to the optical boundary.

The skin surface is non-directional and applies automatically to all 8 EJ-309
placements. When a PMT/SiPM coupling face is added later, override that
single face with a `G4LogicalBorderSurface` and the skin keeps handling the
remaining five faces of every cell.

### Array Layout

| Parameter | Value | Notes |
|---|---|---|
| Number of detectors | 8 | — |
| Arrangement | Forward hemisphere, evenly distributed | — |
| Radius from origin | 500 mm (50 cm) | Flight path for neutron TOF |
| Angular coverage | θ = 30° to 150° in ~20° steps | 4 polar rings × 2 azimuthal positions |
| Sensitive detector | Yes, all 8 | — |

**Positions (spherical coordinates, r = 500 mm):**

| Detector ID | θ (polar) | φ (azimuthal) |
|---|---|---|
| EJ309-0 | 30° | 0° |
| EJ309-1 | 30° | 180° |
| EJ309-2 | 60° | 90° |
| EJ309-3 | 60° | 270° |
| EJ309-4 | 90° | 0° |
| EJ309-5 | 90° | 180° |
| EJ309-6 | 120° | 90° |
| EJ309-7 | 120° | 270° |

**Detection goals:**
- Prompt fission neutrons: detected via proton recoil (n-p elastic scattering
  on hydrogen). Neutron energy reconstructed from time-of-flight: E = ½m(d/t)²
  where d = 500 mm and t is measured relative to trigger t₀.
- Prompt fission gammas: detected via Compton scattering and photoelectric
  absorption. Arrive at t ≈ d/c ≈ 1.7 ns after fission — well separated
  from neutrons in TOF spectrum.
- Neutron/gamma discrimination: via PSD (ratio of slow to fast scintillation
  component), using per-particle-type scintillation yields and time constants.
- Delayed neutrons and gammas: detected at late times (ms to seconds after
  trigger).

**TOF timing reference:**

| Particle | Energy | Velocity | TOF to 500 mm |
|---|---|---|---|
| Gamma | any | c | 1.7 ns |
| Neutron | 0.5 MeV | 0.033c | 51 ns |
| Neutron | 2.0 MeV | 0.065c | 26 ns |
| Neutron | 5.0 MeV | 0.103c | 16 ns |

---

## 3. Inorganic Scintillator Detectors (Backward Angles)

### Material: LaBr₃(Ce) — Lanthanum Bromide doped with Cerium

| Property | Value |
|---|---|
| Composition | LaBr₃ with ~5% Ce doping |
| Density | 5.08 g/cm³ |
| Effective Z | ~46 |
| Light output | ~63,000 photons/MeV |
| Energy resolution | ~2.8% FWHM at 662 keV |
| Decay time | 16 ns |
| Peak emission wavelength | 380 nm |
| Hygroscopic | Yes — must be hermetically sealed in practice |

**Composition:**
- La: 34.85% by mass (Z = 57, A = 138.905)
- Br: 60.14% by mass (Z = 35, A = 79.904)
- Ce: 5.01% by mass (Z = 58, A = 140.116)

The high effective Z is what gives LaBr₃ its gamma spectroscopy advantage —
photoelectric cross section scales as ~Z⁴⁻⁵, so photopeaks are prominent
rather than the Compton-dominated response of organic scintillators.

**Intrinsic backgrounds:** LaBr₃(Ce) carries an internal radioactive
background from ¹³⁸La (t₁/₂ = 1.05 × 10¹¹ yr, 0.089% of natural lanthanum)
producing a continuous spectrum, plus ²²⁷Ac contamination from the raw
material producing alpha peaks.
*Implementation status: NOT modeled in Phase A — see the inline comment in
`src/DetectorConstruction.cc::DefineMaterials()` for the activation path.*

**Per-particle scintillation parameters:** LaBr₃ has a single-component
emission (no PSD lever), so all particles share the same linear yield
curve (63,000 ph/MeV) and `YIELD1 = 1.0` in the MPT. Per-particle entries
for electron, proton, ion, alpha, deuteron, and triton are all populated
because Geant4's `SetScintByParticleType(true)` requires every particle
that can deposit energy here to have a yield curve — there is no fallback
to the global `SCINTILLATIONYIELD` constant in this mode. Real LaBr₃ has
α/β ≈ 0.3 (alpha light output ~30 % of electron-equivalent at the same
energy); the curves should be refined per Moszynski 2006 when photon
scoring is enabled.

### Detector Geometry

| Parameter | Value | Notes |
|---|---|---|
| Shape | Right circular cylinder (G4Tubs) | Standard form factor |
| Diameter | 38 mm (1.5 inches) | Standard commercial size |
| Length | 38 mm (1.5 inches) | — |

### Reflective Wrap

Identical optical surface to the EJ-309 wrap (§2): same `unified` /
`dielectric_dielectric` / `groundfrontpainted`, same R = 0.98 flat across
1.5–4.5 eV, the **same shared `G4OpticalSurface` object** registered as a
second `G4LogicalSkinSurface` — this one on `LaBr3LV`.

Real LaBr₃(Ce) crystals are sold pre-wrapped (typically MgO powder or PTFE)
inside a hermetic Al housing with a quartz/glass window for the PMT. This
simulation places bare crystals directly in the world (no housing per
design.md §3), so the skin surface stands in directly for "the wrapped
crystal as delivered." If/when an explicit hermetic housing is added later,
the skin remains correct on the inner faces and a `G4LogicalBorderSurface`
should be added to model the PMT-window face separately.

### Placement

| Parameter | Value | Notes |
|---|---|---|
| Number of detectors | 2 | Sufficient for gamma spectroscopy |
| Radius from origin | 300 mm (30 cm) | Closer than organic array — higher solid angle compensates for smaller size |
| Positions | Backward angles to minimize neutron exposure | — |
| Sensitive detector | Yes, both | — |

**Positions (spherical coordinates, r = 300 mm):**

| Detector ID | θ (polar) | φ (azimuthal) |
|---|---|---|
| LaBr3-0 | 135° | 45° |
| LaBr3-1 | 135° | 225° |

**Detection goals:**
- Gamma energy spectroscopy with photopeak resolution. Identify specific
  fission product gamma lines (e.g., ¹⁴⁰La at 1596 keV, ⁹⁵Zr at 724/757 keV,
  ¹³³Xe at 81 keV, ¹³⁷Cs at 662 keV).
- Delayed gamma timing relative to fission trigger for fission product
  half-life measurements.
- NOT intended for neutron detection — backward placement reduces neutron
  flux, and LaBr₃ has no PSD capability.

---

## 4. World Volume

| Parameter | Value | Notes |
|---|---|---|
| Material | G4_AIR | Realistic; can switch to vacuum for debugging |
| Geometry | Box (G4Box) | — |
| Dimensions | 2 m × 2 m × 2 m | Large enough to contain all detectors with margin |

---

## 5. Primary Particle Source

| Parameter | Value | Notes |
|---|---|---|
| Particle | Neutron | — |
| Energy | 0.0253 eV | Thermal (T = 293 K, v = 2200 m/s) |
| Direction | +z | Normal to foil face |
| Position | z = −100 mm | Upstream of foil |
| Source profile | Pencil beam | Gaussian spot is a possible variant |

---

## 6. Physics List

| Module | Purpose |
|---|---|
| `G4HadronPhysicsQGSP_BIC_HP` + `G4HadronElasticPhysicsHP` | High-precision neutron transport on ENDF/B-VII data; n + ²³⁵U cross sections below 20 MeV |
| `G4EmStandardPhysics_option4` | Highest-accuracy EM physics |
| `G4OpticalPhysics` (with per-particle-type scintillation enabled) | Scintillation, Cherenkov, optical boundary processes; required for PSD |
| `G4RadioactiveDecayPhysics` | Fission-product β-decay chains, delayed γ/n emission |
| `G4DecayPhysics`, `G4IonPhysics`, `G4IonElasticPhysics`, `G4StoppingPhysics`, `G4EmExtraPhysics` | Decay of unstable particles, ion transport, photo-/electro-nuclear reactions |

Two flags **must** be set before `runManager->Initialize()`:

1. `G4ParticleHPManager::GetInstance()->SetProduceFissionFragments(true)` —
   without it, the HP model deposits fission energy locally instead of
   producing explicit fragment ion tracks. Confirmation appears in the run
   log: *"Fission fragment production is now activated in HP package for
   Z = 92, A = 235"*.
2. `G4OpticalParameters::Instance()->SetScintByParticleType(true)` —
   per-particle scintillation. In Geant4 v11 this flag moved from
   `G4OpticalPhysics::SetScintillationByParticleType` to `G4OpticalParameters`.
   Without it, the per-particle MPT keys (`PROTONSCINTILLATIONYIELD1/2`,
   `ELECTRONSCINTILLATIONYIELD1/2`, …) are silently ignored and PSD cannot
   work — every particle would use the same fast/slow split.

Both flags are set in `nuclear-fission.cc::main()`. With the second flag on,
`G4Scintillation::PostStepDoIt` throws the fatal `Scint01` exception if any
particle deposits energy in a scintillator material that lacks a per-particle
yield entry — which is why `DefineMaterials()` populates electron / proton /
ion / alpha / deuteron / triton curves on **both** EJ-309 and LaBr₃.

---

## 7. Sensitive Detector and Scoring

### Energy-deposition scoring (per step, in every sensitive volume)

- Energy deposited (MeV)
- Global time (ns, relative to event start)
- Particle type (PDG encoding)
- Track ID and parent ID
- Process that created the particle
- dE/dx at each step (for Birks' law correction in post-processing)
- Pre-step and post-step position

### Optical-photon scoring (at photocathode surfaces)

- Number of optical photons arriving per event
- Arrival time of each optical photon
- Wavelength of each optical photon

Optical-photon scoring is what enables waveform reconstruction and PSD
analysis from simulated pulses.

---

## 8. Analysis Observables

### From the trigger detector:
- Fission event rate and timing (t₀)
- Trigger pulse height spectrum (verify fragment/alpha/gamma separation)

### From the EJ-309 array:
- Neutron time-of-flight spectrum → prompt fission neutron energy spectrum
- PSD scatter plot (tail-to-total ratio vs. light output)
- Prompt neutron multiplicity distribution
- Prompt gamma multiplicity
- Delayed radiation time profiles

### From the LaBr₃ detectors:
- Gamma energy spectrum with photopeaks
- Identification of fission product gamma lines
- Delayed gamma time profiles for half-life extraction

---

## 9. Implementation Status

This document describes the *full designed system*. The simulation is built
in phases; what is and isn't yet implemented at the time of writing is
recorded here. See `doc/architecture.md` for the code-flow side and
`doc/plan.md` for the phased implementation plan.

### Phase A — Geometry, materials, generator, mode dispatch (DONE)

| Component | Status | Notes |
|---|---|---|
| 2 m × 2 m × 2 m air world | ✓ implemented | `G4Box`, `G4_AIR`. |
| ²³⁵U foil (20 mm dia × 0.5 µm) | ✓ implemented | `G4Tubs`, normal ∥ ẑ, 100 % enriched. |
| Trigger scintillator (§1.C) | ✗ deferred | Not needed in simulation — fission t=0 is recovered from the Geant4 vertex directly. May be added later when modelling a real DAQ. |
| Backing plate (§1.D) | ✗ deferred | Mechanical-support analog; not load-bearing for physics. |
| 8 × EJ-309 cylinders + 1 mm Al housing | ✓ implemented | Cylinder axes oriented radially. Housing now required (was "optional" in spec). |
| 2 × LaBr₃(Ce) cylinders | ✓ implemented | Bare (no housing per spec). |
| Tyvek/PTFE diffuse reflective skin (EJ-309 + LaBr₃) | ✓ implemented | Shared `G4OpticalSurface` (`unified` / `dielectric_dielectric` / `groundfrontpainted`, R = 0.98 flat); attached as `G4LogicalSkinSurface` on `EJ309LV` and `LaBr3LV`. |
| EJ-309 MPT incl. per-particle yields | ✓ implemented | electron / proton / ion / alpha / deuteron / triton curves. |
| LaBr₃ MPT incl. per-particle yields | ✓ implemented | Linear curves (single-component, no PSD). Real α/β ≈ 0.3 quenching deferred to refinement pass. |
| Stilbene material variant (§2) | ✗ deferred | EJ-309 only. |
| LaBr₃ intrinsic ¹³⁸La / ²²⁷Ac backgrounds (§3) | ✗ deferred | Not modelled; activation path documented in `DefineMaterials()` and below. |
| Pencil-beam neutron source at z = −100 mm, +ẑ | ✓ implemented | Thermal (0.025 eV). |
| Physics list (modular, all per §6 modules) | ✓ implemented | Including the two pre-Initialize flags. |
| Mode dispatch (interactive vs batch) | ✓ implemented | `./nuclear_fission` → OGL via `vis.mac`; `./nuclear_fission run.mac` → headless. |

### Phase B — Sensitive detectors + ground-truth `hits.csv` (PENDING)

`ScintillatorSD`, `HitWriter`, `EventWriter`, the timestamped output
directory under `data/<UTC>/`, and the wiring from `MyRunAction` /
`MyEventAction` / `MyDetectorConstruction::ConstructSDandField()`. After
Phase B the simulation produces one row per (track, sensitive volume)
entry: `event_id, detector_id, track_id, particle, creator_process,
entry_time_ns, energy_dep_MeV`.

### Phase C — Fission watcher + `events.csv` (PENDING)

`MySteppingAction` matches the `nFissionHP` post-step process and captures
fission-vertex time + prompt-n/γ multiplicities + the two fragment PDGs.
One row per event in `events.csv`.

### Deferred beyond Phase C

- Optical photon scoring (a photocathode surface SD, or scoring photons
  at the scintillator boundary). The MPT and `SetScintByParticleType`
  flag are already in place — see `doc/architecture.md` "Optical
  infrastructure (deferred)" for the flip-on path.
- SiPM / PMT digitizer model.
- Trigger scintillator (§1.C).
- Backing plate (§1.D).
- Stilbene material variant.
- LaBr₃ intrinsic backgrounds (¹³⁸La continuous β + 1436 keV γ; ²²⁷Ac
  α peaks). Activation path: mix natural-La and trace ²²⁷Ac into the
  LaBr₃ material and let `G4RadioactiveDecayPhysics` handle them, OR
  fire from a `G4GeneralParticleSource` confined to the LaBr₃ logical
  volume at the appropriate Bq/cm³ rate.
- `G4NeutronTrackingCut` raised time limit for delayed-neutron coverage
  (out of scope for prompt-only window).

