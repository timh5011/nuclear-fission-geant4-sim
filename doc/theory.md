# Theory

This document covers the physics modeled by this simulation. Per the conventions in CLAUDE.md, every process is explicitly classified as belonging to the **Source stage** (neutron-on-uranium and everything downstream of it inside the target) or the **Detector/DAQ stage** (scintillator response, photodetector, digitization). The fission section below is entirely Source stage. The detector section will be added once the scintillator geometry is implemented.

---

## Part I — Source Stage: Thermal Neutron Fission of ²³⁵U

### Summary of Produced Particles

The table below gives every particle family produced in a single fission event, from the moment the neutron enters the foil to the end of the decay chain. Energies are per-fission averages, summed over both fragments and all decay steps.

| Particle family | When produced | Multiplicity (per fission) | Energy carried (MeV) |
|---|---|---|---|
| **Fission fragments** (two heavy ions) | Prompt (< 10⁻²⁰ s after scission) | 2 | ~167 MeV total KE |
| **Prompt fission neutrons** | Prompt (< 10⁻¹⁴ s) | ν̄ = 2.43 | ~4.8 MeV total KE |
| **Prompt fission gammas** | Prompt (< 10⁻¹⁰ s) | ~7–8 photons | ~7 MeV total |
| **Beta electrons** (β⁻ from decay chains) | Delayed (ms → years) | ~6 per fission | ~8 MeV total KE (shared with ν̄) |
| **Decay gammas** (from excited daughters) | Delayed (ms → years) | ~6–10 photons | ~7 MeV total |
| **Delayed neutrons** (β-delayed) | Delayed (0.2 s → 56 s half-lives) | 0.0158 n/fission (0.65% of fissions) | 0.2–0.6 MeV each |
| **Antineutrinos** (from β⁻ decay) | Delayed, same timing as betas | ~6 per fission | ~12 MeV total (undetectable) |

**Total recoverable energy** (everything except antineutrinos): ~195 MeV per fission.  
**Total Q-value** (including antineutrino energy): ~202 MeV per fission.

**Decay chain schematic** — each fission fragment undergoes a chain of β⁻ decays, with gamma emission from excited daughters at each step, until a stable nuclide is reached. A small fraction of steps produce β-delayed neutrons instead of (or in addition to) a gamma:

```
n + ²³⁵U ──────────────────────────────────────────────────── compound nucleus ²³⁶U*
                                                                        │
                                              ┌─────────────────────────┤
                                              │                         │
                              Light fragment (A ~ 95)      Heavy fragment (A ~ 139)
                              T_KE ~ 100 MeV              T_KE ~ 67 MeV
                              + prompt neutrons (ν̄ = 2.43 total)
                              + prompt gammas  (~7–8 photons)
                                              │                         │
                              β⁻ decay chain (2–8 steps)   β⁻ decay chain (2–8 steps)
                              each step:                    each step:
                                β⁻ + ν̄_e                    β⁻ + ν̄_e
                                γ (if daughter excited)      γ (if daughter excited)
                                [occasionally: delayed n]    [occasionally: delayed n]
                                              │                         │
                                         stable isobar             stable isobar
```

---

### 1. The Fission Reaction

#### 1.1 Compound Nucleus Formation

When a thermal neutron (kinetic energy E_n = 0.025 eV) is captured by ²³⁵U, the system forms the compound nucleus ²³⁶U in a highly excited state:

```
n + ²³⁵U → ²³⁶U*
```

The excitation energy of ²³⁶U* is the sum of the neutron's kinetic energy and the neutron separation energy S_n of ²³⁶U:

```
E* = S_n + E_n = 6.545 MeV + 0.025 eV ≈ 6.54 MeV
```

This excitation energy exceeds the fission barrier of ²³⁶U (B_f ≈ 5.8 MeV) by ~0.74 MeV, which is why ²³⁵U fissions so readily with thermal neutrons. The compound nucleus has no memory of how it was formed — it lives for ~10⁻¹⁴ to 10⁻¹⁵ s before either fissioning, emitting a gamma (radiative capture), or emitting a neutron. For thermal incident neutrons, fission dominates strongly over capture.

#### 1.2 Fission Cross Section

The thermal neutron cross sections for ²³⁵U at E_n = 0.025 eV (v = 2200 m/s):

| Reaction | Cross section |
|---|---|
| Fission (n,f) | σ_f = 584.3 barns |
| Radiative capture (n,γ) | σ_γ = 98.6 barns |
| Total absorption | σ_abs = 682.9 barns |
| Elastic scattering | σ_el ≈ 15 barns |

The ratio of capture to fission, α = σ_γ/σ_f ≈ 0.169, means that for every 6 fissions roughly 1 capture occurs. The cross section follows a strict 1/v dependence at thermal energies (σ ∝ 1/v ∝ E^-1/2), so lower-energy neutrons are even more reactive. Above ~1 eV the cross section enters a resolved resonance region; the first major fission resonance is at 0.29 eV with σ_f ≈ 1100 barns.

In this simulation, the incident neutron energy is 0.025 eV, placing it squarely in the thermal regime where the HP cross-section data from G4NDL (ENDF/B-VII.0) is directly applicable.

#### 1.3 Q-Value and Energy Partition

The total energy released per fission event (~202 MeV) is distributed among the particle families as follows. These are averages over all fission modes (all possible fragment pairs), weighted by their yield.

| Component | Average energy (MeV) | Recoverable in detector? |
|---|---|---|
| Kinetic energy of fission fragments | 167.3 | Yes (ionization in foil or scintillator) |
| Kinetic energy of prompt neutrons | 4.8 | Partial (moderation, then capture or escape) |
| Energy of prompt gammas | 7.0 | Yes (Compton/photo-electric) |
| Kinetic energy of β⁻ particles (decay chains) | 8.0 | Yes (ionization in scintillator) |
| Energy of decay gammas | 7.0 | Yes (Compton/photo-electric) |
| Energy of antineutrinos | 12.0 | No (escape) |
| **Total** | **206.1** | **~194 MeV** |

The dominant term by far is fragment kinetic energy — 82% of the total — but those fragments stop in microns inside the foil and do not reach the scintillator under the current geometry.

---

### 2. Fission Fragments

**Source stage.** Production mechanism: after the compound nucleus deforms to the saddle point, the neck ruptures and the two nascent fragments are accelerated apart by their mutual Coulomb repulsion. This happens on a timescale of ~10⁻²⁰ s. The fragments at the moment of scission are called *primary fragments*; they immediately boil off prompt neutrons and become *secondary fragments*, which then undergo radioactive decay.

#### 2.1 Mass Distribution

Thermal neutron fission of ²³⁵U produces an asymmetric, bimodal mass yield distribution — symmetric splits are strongly suppressed relative to asymmetric ones. The asymmetry is driven by nuclear shell effects: the heavy fragment preferentially lands near the doubly-magic Z=50, N=82 closed shells (~¹³²Sn), stabilizing that configuration.

The mass yield Y(A) (percent of fissions producing a fragment of mass A — note the sum over all A is 200% since two fragments are produced per fission):

- **Light peak:** centered at A ≈ 90–100, maximum around A = 95 (Zr, Mo, Nb region). Peak yield ~6–7% per mass unit.
- **Heavy peak:** centered at A ≈ 130–145, maximum around A = 139 (Ba, La, Cs region). Peak yield ~6–7% per mass unit.
- **Valley (symmetric splits):** A ≈ 115–120, yield < 0.01% per mass unit — suppressed by ~3 orders of magnitude relative to the peaks.

The most probable individual split is approximately A_L = 95, A_H = 140 (since the compound nucleus has A = 236, the two fragments must sum to 236 minus the prompt neutrons emitted). For a 3-neutron emission event: 95 + 138 + 3 = 236.

Some important high-yield fragment pairs and their cumulative fission yields:
- ⁹⁴Sr / ¹³⁹Xe (+ 3n): cumulative yield ~5.8%
- ⁹²Kr / ¹⁴¹Ba (+ 3n): ~5.1%  
- ⁹⁰Sr / ¹⁴³Xe (+ 3n): ~3.9%

#### 2.2 Charge Distribution

For a given mass split (A_L, A_H), the charge is distributed roughly according to the *unchanged charge density* (UCD) assumption: Z_fragment/A_fragment ≈ Z_compound/A_compound = 92/236. In practice the charge distribution is approximately Gaussian in Z for fixed A, with width σ_Z ≈ 0.5–0.6 charge units. This produces a range of neutron-rich isobars for each mass chain, all of which will ultimately β-decay to stability.

#### 2.3 Kinetic Energies

Momentum conservation requires the two fragments to recoil back-to-back in the center-of-mass frame. Since p_L = p_H, their kinetic energies scale inversely with mass:

```
T_L / T_H = A_H / A_L
```

For the most probable split (A_L ≈ 95, A_H ≈ 139):
- Light fragment: T_L ≈ **100 MeV**
- Heavy fragment: T_H ≈ **67 MeV**
- Total kinetic energy (TKE): **~167 MeV**

The TKE varies with the specific mass split. The Viola systematics provide an empirical estimate:

```
TKE ≈ 0.1189 × (Z₁Z₂) / (A₁^(1/3) + A₂^(1/3)) + 7.3 MeV
```

For asymmetric splits near the most probable this gives TKE ≈ 165–170 MeV; symmetric splits have lower TKE (~130 MeV) because the fragments are less deformed.

#### 2.4 Stopping and Range in Matter

Fission fragments are highly charged heavy ions (Z ~ 35–55, A ~ 90–145) with kinetic energies of 60–100 MeV. Their stopping power in matter is enormous — dominated by electronic stopping (Bethe-Bloch) and the high charge. In the ²³⁵U foil itself (density ~18.9 g/cm³):

- Light fragment (~95 u, ~100 MeV): range ≈ 6–8 µm in uranium
- Heavy fragment (~139 u, ~67 MeV): range ≈ 5–7 µm in uranium

The foil in this simulation is 0.5 µm thick, so **most fragments exit the back face of the foil** (the fragment born traveling in the −z direction is absorbed in the foil; the one born in +z escapes into air). In air both fragments stop within ~2–4 cm. In plastic scintillator (density ~1.05 g/cm³) the range increases to ~30–50 µm — still very short compared to typical detector dimensions.

In the OGL viewer, fission fragments appear as very short blue stubs at the foil face. This is physically correct behavior, not a bug.

---

### 3. Prompt Fission Neutrons

**Source stage.** Production mechanism: the nascent fragments immediately after scission are highly excited (excitation energy of ~20–30 MeV each) and evaporate neutrons on a timescale of ~10⁻¹⁴ s — before any gamma emission, before any beta decay. These are *prompt* neutrons, emitted isotropically in the rest frame of each fragment but boosted into the lab frame by the fragment's velocity (~1.4% of c for the light fragment). The result is a slight forward-backward asymmetry in the lab, but no strong directionality.

#### 3.1 Multiplicity

The prompt neutron multiplicity ν follows a near-Poisson distribution. For ²³⁵U + thermal neutron:

- **Mean prompt multiplicity:** ν̄_p = **2.4198** (ENDF/B-VIII.0)
- **Mean delayed multiplicity:** ν̄_d = 0.0157
- **Total mean:** ν̄ = **2.4355**

The probability distribution P(ν) (fraction of fissions producing exactly ν prompt neutrons):

| ν | P(ν) |
|---|---|
| 0 | ~0.030 |
| 1 | ~0.171 |
| 2 | ~0.336 |
| 3 | ~0.302 |
| 4 | ~0.126 |
| 5 | ~0.030 |
| 6 | ~0.005 |

Each neutron carries energy drawn independently from the Watt spectrum (see below).

#### 3.2 Energy Spectrum — The Watt Spectrum

The prompt fission neutron energy spectrum for ²³⁵U + thermal neutron is well described by the **Watt fission spectrum**:

```
χ(E) = C · exp(−E/a) · sinh(√(bE))
```

where C is a normalization constant and the ENDF/B-VII parameters for ²³⁵U are:

```
a = 0.988 MeV
b = 2.249 MeV⁻¹
```

Key spectral properties:

| Property | Value |
|---|---|
| Most probable energy | ~0.7 MeV |
| Mean energy | ~2.0 MeV |
| Median energy | ~1.3 MeV |
| Practical upper limit | ~12 MeV |
| Total KE (all prompt n) | ν̄ × Ē ≈ 2.43 × 2.0 ≈ **4.8 MeV** |

The spectrum rises steeply from zero (the sinh factor), peaks around 0.7 MeV, and falls exponentially above the peak. Very few neutrons exceed 8–10 MeV. The shape arises from the Maxwellian-like evaporation spectrum of each fragment, convolved with the fragment velocity distribution.

This is the spectrum that Geant4's HP package samples when `G4ParticleHPManager::SetProduceFissionFragments(true)` is set and `G4HadronPhysicsQGSP_BIC_HP` is registered. The G4NDL data tables (derived from ENDF/B-VII) are used directly.

#### 3.3 Fate of Prompt Neutrons in This Geometry

With ν̄ ≈ 2.43 fast neutrons born per fission at ~2 MeV average:
- In an infinite ²³⁵U medium they would sustain a chain reaction (k_eff = 1 with sufficient mass).
- In this simulation the foil is 0.5 µm thick in a 20 cm air world — the neutrons escape into air immediately and reach the world boundary. Essentially none are re-captured in the foil on the timescale of a single event.
- In the OGL viewer, prompt neutrons appear as **green tracks** extending to the world boundary.
- When a plastic scintillator is added, fast neutrons can scatter off hydrogen nuclei (proton recoil), producing additional ionization. This is the basis of fast-neutron detection in organic scintillators.

---

### 4. Prompt Fission Gammas

**Source stage.** Production mechanism: after the fragments have evaporated their prompt neutrons (reducing their excitation energy by ~1–2 MeV per neutron), they are still highly excited and de-excite via statistical gamma emission on a timescale of ~10⁻¹⁰ s. These are the prompt fission gammas. They are distinct from the delayed gammas that accompany the subsequent β-decay chain.

#### 4.1 Multiplicity

- **Mean number of prompt gammas:** N̄_γ ≈ **7–8 photons per fission**
- **Total prompt gamma energy:** Ē_tot ≈ **7 MeV per fission**
- **Mean photon energy:** Ē_γ ≈ 1 MeV

The multiplicity fluctuates significantly event-by-event, anti-correlated with the neutron multiplicity (more neutrons → less excitation energy remaining → fewer gammas).

#### 4.2 Energy Spectrum

The prompt gamma spectrum for ²³⁵U thermal fission spans roughly 0.1 to 7 MeV. The differential yield (photons per MeV per fission) is approximately:

- **Below 1 MeV:** relatively flat, moderate yield — soft gammas from collective transitions and low-energy statistical de-excitation.
- **1–3 MeV:** falls roughly exponentially. This is the dominant energetic region; most of the total energy is carried by gammas in the 0.5–3 MeV band.
- **Above 3 MeV:** falls steeply. Hard gammas (> 4 MeV) arise from giant dipole resonance (GDR) transitions during the deformation phase near scission, but their yield is small.
- **Maximum observed:** ~8 MeV.

A commonly used empirical parameterization for the spectral shape above ~0.5 MeV:

```
dN/dE ∝ exp(−E / E₀),  with E₀ ≈ 0.8 MeV
```

#### 4.3 Timing and Source

Prompt gammas are emitted in two broad stages:

1. **Scission gammas** (bremsstrahlung-like, during neck rupture): very short timescale (~10⁻²⁰ s), contribute a small fraction of the total yield; the GDR gammas fall here.
2. **Fragment de-excitation gammas** (statistical evaporation): emitted after prompt neutron emission, ~10⁻¹⁴ to 10⁻¹⁰ s; this is the dominant source.

On any experimental timescale both contributions are instantaneous — the prompt gamma burst coincides with the fission event itself.

In the OGL viewer, prompt gammas (along with prompt neutrons) appear as **green tracks** extending to the world boundary.

---

### 5. Fission Fragment Decay Chains

**Source stage** (except where a decay product enters the scintillator — that boundary-crossing is Source→Detector and will be called out in Part II).

Both fission fragments land far from the valley of beta stability. Fragments in the light peak (A ~ 90–100) typically have 8–12 more neutrons than the stable isobar; fragments in the heavy peak (A ~ 130–145) have 6–10 excess neutrons. Each fragment undergoes a chain of β⁻ decays — emitting an electron and an antineutrino at each step — until it reaches a stable nuclide. Along the way, excited daughter states emit gamma rays, and a small fraction of decays produce delayed neutrons.

The average chain length is **3–4 β-decay steps per fragment**, or roughly **6–8 total per fission event**. Chain lengths range from 2 (for fragments that land close to stability) to 8+ (for the most neutron-rich primary fragments).

---

#### 5.1 Beta Electrons (β⁻)

**Production:** each β⁻ decay converts a neutron into a proton in the daughter nucleus, emitting an electron and an electron antineutrino. The Q-value of the decay is shared statistically between the electron and the antineutrino.

**Multiplicity:** ~6 β⁻ electrons per fission event on average (sum over both fragment chains).

**Energy spectrum:** each individual β⁻ has a continuous spectrum from zero to the endpoint Q_β, with the shape governed by phase space and the Fermi function:

```
dN/dE ∝ p · E · (Q_β − E)² · F(Z', E)
```

where p is the electron momentum, F(Z', E) is the Fermi function for the daughter nucleus of charge Z', and (Q_β − E)² is the phase space factor for the antineutrino. The mean electron energy is approximately one-third of the endpoint: Ē_e ≈ Q_β/3.

Endpoint energies Q_β for fission products span a wide range:

| Chain position | Typical Q_β range | Example isotope |
|---|---|---|
| First generation (most neutron-rich) | 5–10 MeV | ⁹⁴Rb → ⁹⁴Sr: Q_β = 9.9 MeV |
| Middle of chain | 2–5 MeV | ⁹⁴Sr → ⁹⁴Y: Q_β = 5.9 MeV |
| Near stability | 0.5–2 MeV | ⁹⁴Y → ⁹⁴Zr: Q_β = 4.9 MeV |
| Long-lived (close to stable) | 0.1–0.5 MeV | ⁹⁰Sr → ⁹⁰Y: Q_β = 0.546 MeV |

Summed over all decays in both chains, the total β⁻ kinetic energy per fission is **~8 MeV** (with a comparable ~8 MeV going to antineutrinos, summing to the ~16 MeV total Q_β averaged over all chains; exact totals depend on which fragments are produced).

**Timescale:** determined by the half-lives of the precursor isotopes, which span an enormous range:
- Short-lived precursors: milliseconds to seconds (e.g., ⁸⁷Br: T₁/₂ = 55.7 s, but daughters like ⁸⁷Kr: T₁/₂ = 76 min)
- Long-lived fission products: Cs-137 (T₁/₂ = 30.2 years), Sr-90 (T₁/₂ = 28.8 years)

In a single-event Geant4 run, the decay chain walks forward in simulation time; `G4RadioactiveDecayPhysics` handles the time-ordered decay sequence and produces the β and γ tracks.

In the OGL viewer, beta electrons appear as **red tracks** (the standard Geant4 color for electrons), predominantly near the foil.

**Example decay chains** (two representative chains illustrating different chain lengths and timescales):

*Light fragment chain — Krypton-92:*
```
⁹²Kr (T₁/₂ = 1.84 s)  →  ⁹²Rb (T₁/₂ = 4.49 s)  →  ⁹²Sr (T₁/₂ = 2.71 h)
    → ⁹²Y (T₁/₂ = 3.54 h)  →  ⁹²Zr [STABLE]
    Q_β steps: 9.9 → 8.1 → 1.9 → 3.6 MeV
```

*Heavy fragment chain — Barium-141:*
```
¹⁴¹Ba (T₁/₂ = 18.3 min)  →  ¹⁴¹La (T₁/₂ = 3.92 h)
    →  ¹⁴¹Ce (T₁/₂ = 32.5 d)  →  ¹⁴¹Pr [STABLE]
    Q_β steps: 3.0 → 2.5 → 0.58 MeV
```

*Long-lived chain — Strontium-90 (a significant environmental contaminant from fission):*
```
⁹⁰Sr (T₁/₂ = 28.8 y, Q_β = 0.546 MeV)  →  ⁹⁰Y (T₁/₂ = 64.1 h, Q_β = 2.28 MeV)
    →  ⁹⁰Zr [STABLE]
```

---

#### 5.2 Delayed Gammas

**Production:** most β⁻ decays leave the daughter nucleus in an excited state (not the ground state). The daughter de-excites by emitting one or more gamma rays. This is the source of the delayed gamma radiation from fission products.

**Multiplicity:** approximately **6–10 delayed gammas per fission** (one or more per β⁻ decay step, depending on how many excited levels are populated in the daughter).

**Energy range:** 0.05 to ~3 MeV per photon, with most of the yield below 2 MeV.

**Total delayed gamma energy:** **~7 MeV per fission** (averaged over all decay chains and fission modes).

**Timescale:** same as the β⁻ decays — the gamma immediately follows the β⁻ from the same nuclide, so the gamma timing is set by the parent half-life.

Some of the most intense and practically important delayed gamma lines:

| Decay | γ energy | T₁/₂ of parent | Why notable |
|---|---|---|---|
| ¹³⁷Cs → ¹³⁷ᵐBa → ¹³⁷Ba | 661.7 keV | 30.2 y → 2.55 min | The standard calibration line; dominant in spent-fuel gamma spectra |
| ¹³¹I → ¹³¹Xe | 364.5 keV | 8.02 d | Thyroid dose concern; used in nuclear medicine |
| ¹⁴⁰Ba → ¹⁴⁰La → ¹⁴⁰Ce | 1596 keV (La) | 12.75 d → 1.68 d | Strong line in fresh fission product mixtures |
| ⁹⁵Zr → ⁹⁵Nb → ⁹⁵Mo | 757 keV (Nb) | 64 d → 35 d | Prominent in medium-lived waste |

Delayed gammas are in principle detectable in the scintillator at any time after the fission event; for long-lived chains like Cs-137, the source persists essentially indefinitely on laboratory timescales.

---

#### 5.3 Delayed Neutrons

**Production:** a small fraction of β⁻ decays produce a *delayed neutron*. The mechanism is: the precursor nucleus (the neutron-rich fission product) undergoes β⁻ decay, leaving the daughter in a highly excited state with excitation energy *above the neutron separation energy* S_n of the daughter. The daughter then promptly emits a neutron. Because the delay is set by the β⁻ decay half-life (not by the fast neutron-emission step), these neutrons appear delayed relative to fission.

**Total yield:**
- Delayed neutrons per fission: ν̄_d = **0.0158** (ENDF/B-VIII.0)
- Total neutrons per fission: ν̄ = 2.4355
- Delayed neutron fraction: **β = ν̄_d / ν̄ = 0.0065 (0.65%)**

Despite the tiny fraction, delayed neutrons are what make nuclear reactors controllable: they extend the effective neutron lifetime from ~10⁻⁵ s (prompt alone) to ~0.1 s, allowing mechanical control systems to respond.

**Six-group representation** (Keepin group parameters for ²³⁵U + thermal fission):

| Group | T₁/₂ (s) | Fraction of β | Mean energy | Representative precursor |
|---|---|---|---|---|
| 1 | 55.7 | 3.3% | ~0.25 MeV | ⁸⁷Br |
| 2 | 22.7 | 21.9% | ~0.46 MeV | ¹³⁷I |
| 3 | 6.22 | 19.6% | ~0.41 MeV | ⁸⁸Br, ¹³⁸I |
| 4 | 2.30 | 39.5% | ~0.45 MeV | ⁹³Rb, ¹⁴²Cs |
| 5 | 0.61 | 11.5% | ~0.41 MeV | ⁸⁵As, ¹³⁴Sb |
| 6 | 0.23 | 4.2% | ~0.43 MeV | ⁸⁴As, ¹³³Sn |

The group structure arises from clustering ~100+ individual precursor isotopes by their half-lives. Groups 4–6 carry the bulk of the yield but decay quickly; Group 1 is the longest-lived and sustains the delayed neutron flux over minute timescales.

**Energy spectrum:** delayed neutrons are emitted at significantly lower energies than prompt neutrons (Watt peak ~0.7 MeV, mean ~2 MeV). Delayed neutrons are essentially monoenergetic or near-monoenergetic *per precursor* (because they are emitted from a specific excited state of the daughter nucleus), but the ensemble spectrum is a broad smear over 0.1–1.5 MeV due to the many precursor species. The mean is ~0.4 MeV.

**In this simulation:** `G4RadioactiveDecayPhysics` handles the β-decay chains that produce delayed neutrons, but whether a given precursor's delayed neutron branch is included depends on the completeness of the ENSDF data in the installed G4NDL. At the 0.025 eV incident energy the delayed neutron yield is small and their contribution to any single simulated event is negligible, but they appear correctly in the decay chain if the simulation runs long enough in time.

---

#### 5.4 Antineutrinos

**Production:** every β⁻ decay emits one electron antineutrino (ν̄_e), carrying away a share of the Q-value statistically determined by the same phase-space distribution as the electron.

**Multiplicity:** **~6 antineutrinos per fission** (one per β⁻ decay step, ~6 decays on average).

**Total energy:** **~12 MeV per fission** (mean ~2 MeV per antineutrino).

**Detectability:** antineutrinos interact only via the weak force. The inverse beta decay cross section (the most favorable detection reaction: ν̄_e + p → e⁺ + n) is ~10⁻⁴¹ cm² at a few MeV. They escape every realistic detector with probability ~1 and are undetectable in this simulation.

Geant4's `G4RadioactiveDecayPhysics` does produce antineutrino tracks internally, but they are immediately killed (zero mean free path in the default configuration) and deposit no energy. They do not appear in the OGL viewer and do not affect any scoring.

The ~12 MeV they carry away is therefore irretrievably lost, which is why the *recoverable* energy per fission is ~190–195 MeV rather than the full ~202 MeV Q-value.
