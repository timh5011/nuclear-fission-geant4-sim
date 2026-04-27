# Theory

This document covers the physics modeled by this simulation. Per the conventions in CLAUDE.md, every process is explicitly classified as belonging to the **Source stage** (neutron-on-uranium and everything downstream of it inside the target) or the **Detector/DAQ stage** (scintillator response, photodetector, digitization). The fission section below is entirely Source stage. The detector section will be added once the scintillator geometry is implemented.

---

## Part I — Source Stage: Thermal Neutron Fission of ²³⁵U

### Summary of Produced Particles

The table below gives every particle family produced in a single fission event, from the moment the neutron enters the foil to the end of the decay chain. Energies are per-fission averages, summed over both fragments and all decay steps.

| Particle family | When produced | Multiplicity (per fission) | Energy carried (MeV) |
|---|---|---|---|
| **Fission fragments** (two heavy ions) | Prompt (< 10⁻²⁰ s after scission) | 2 | ~167 MeV total KE |
| **Prompt fission neutrons** | Prompt (< 10⁻¹⁴ s) | $\bar{\nu} = 2.43$ | ~4.8 MeV total KE |
| **Prompt fission gammas** | Prompt (< 10⁻¹⁰ s) | ~7–8 photons | ~7 MeV total |
| **Beta electrons** (β⁻ from decay chains) | Delayed (ms → years) | ~6 per fission | ~8 MeV total KE (shared with $\bar{\nu}_e$) |
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

When a thermal neutron (kinetic energy $E_n = 0.025$ eV) is captured by ²³⁵U, the system forms the compound nucleus ²³⁶U in a highly excited state:

$$n + {}^{235}\text{U} \;\rightarrow\; {}^{236}\text{U}^*$$

The excitation energy of ²³⁶U* is the sum of the neutron's kinetic energy and the neutron separation energy $S_n$ of ²³⁶U:

$$E^* = S_n + E_n = 6.545\,\text{MeV} + 0.025\,\text{eV} \approx 6.54\,\text{MeV}$$

This excitation energy exceeds the fission barrier of ²³⁶U ($B_f \approx 5.8$ MeV) by ~0.74 MeV, which is why ²³⁵U fissions so readily with thermal neutrons. The compound nucleus has no memory of how it was formed — it lives for ~10⁻¹⁴ to 10⁻¹⁵ s before either fissioning, emitting a gamma (radiative capture), or emitting a neutron. For thermal incident neutrons, fission dominates strongly over capture.

#### 1.2 Fission Cross Section

The thermal neutron cross sections for ²³⁵U at $E_n = 0.025$ eV ($v = 2200$ m/s):

| Reaction | Cross section |
|---|---|
| Fission (n,f) | $\sigma_f = 584.3$ barns |
| Radiative capture (n,γ) | $\sigma_\gamma = 98.6$ barns |
| Total absorption | $\sigma_\text{abs} = 682.9$ barns |
| Elastic scattering | $\sigma_\text{el} \approx 15$ barns |

The ratio of capture to fission, $\alpha = \sigma_\gamma/\sigma_f \approx 0.169$, means that for every 6 fissions roughly 1 capture occurs. The cross section follows a strict $1/v$ dependence at thermal energies ($\sigma \propto 1/v \propto E^{-1/2}$), so lower-energy neutrons are even more reactive. Above ~1 eV the cross section enters a resolved resonance region; the first major fission resonance is at 0.29 eV with $\sigma_f \approx 1100$ barns.

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

Thermal neutron fission of ²³⁵U produces an asymmetric, bimodal mass yield distribution — symmetric splits are strongly suppressed relative to asymmetric ones. The asymmetry is driven by nuclear shell effects: the heavy fragment preferentially lands near the doubly-magic $Z=50$, $N=82$ closed shells (~¹³²Sn), stabilizing that configuration.

The mass yield $Y(A)$ (percent of fissions producing a fragment of mass $A$ — note the sum over all $A$ is 200% since two fragments are produced per fission):

- **Light peak:** centered at $A \approx 90$–$100$, maximum around $A = 95$ (Zr, Mo, Nb region). Peak yield ~6–7% per mass unit.
- **Heavy peak:** centered at $A \approx 130$–$145$, maximum around $A = 139$ (Ba, La, Cs region). Peak yield ~6–7% per mass unit.
- **Valley (symmetric splits):** $A \approx 115$–$120$, yield < 0.01% per mass unit — suppressed by ~3 orders of magnitude relative to the peaks.

The most probable individual split is approximately $A_L = 95$, $A_H = 139$ (since the compound nucleus has $A = 236$, the two fragments must sum to 236 minus the prompt neutrons emitted). For a 2-neutron emission event: $95 + 139 + 2 = 236$.

Some important high-yield fragment pairs and their cumulative fission yields:
- ⁹⁴Sr / ¹³⁹Xe (+ 3n): cumulative yield ~5.8%
- ⁹²Kr / ¹⁴¹Ba (+ 3n): ~5.1%  
- ⁹⁰Sr / ¹⁴³Xe (+ 3n): ~3.9%

#### 2.2 Charge Distribution

For a given mass split $(A_L, A_H)$, the charge is distributed roughly according to the *unchanged charge density* (UCD) assumption: $Z_\text{frag}/A_\text{frag} \approx Z_\text{comp}/A_\text{comp} = 92/236$. In practice the charge distribution is approximately Gaussian in $Z$ for fixed $A$, with width $\sigma_Z \approx 0.5$–$0.6$ charge units. This produces a range of neutron-rich isobars for each mass chain, all of which will ultimately β-decay to stability.

#### 2.3 Kinetic Energies

Momentum conservation requires the two fragments to recoil back-to-back in the center-of-mass frame. Since $p_L = p_H$, their kinetic energies scale inversely with mass:

$$\frac{T_L}{T_H} = \frac{A_H}{A_L}$$

For the most probable split ($A_L \approx 95$, $A_H \approx 139$):
- Light fragment: $T_L \approx$ **100 MeV**
- Heavy fragment: $T_H \approx$ **67 MeV**
- Total kinetic energy (TKE): **~167 MeV**

The TKE varies with the specific mass split. The Viola systematics provide an empirical estimate:

$$\text{TKE} \approx \frac{0.1189\; Z_1 Z_2}{A_1^{1/3} + A_2^{1/3}} + 7.3\,\text{MeV}$$

For asymmetric splits near the most probable this gives TKE ≈ 165–170 MeV; symmetric splits have lower TKE (~130 MeV) because the fragments are less deformed.

#### 2.4 Stopping and Range in Matter

Fission fragments are highly charged heavy ions ($Z \sim 35$–$55$, $A \sim 90$–$145$) with kinetic energies of 60–100 MeV. Their stopping power in matter is enormous — dominated by electronic stopping (Bethe-Bloch) and the high charge. In the ²³⁵U foil itself (density ~18.9 g/cm³):

- Light fragment (~95 u, ~100 MeV): range ≈ 6–8 µm in uranium
- Heavy fragment (~139 u, ~67 MeV): range ≈ 5–7 µm in uranium

The foil in this simulation is 0.5 µm thick, so **most fragments exit the back face of the foil** (the fragment born traveling in the −z direction is absorbed in the foil; the one born in +z escapes into air). In air both fragments stop within ~2–4 cm. In plastic scintillator (density ~1.05 g/cm³) the range increases to ~30–50 µm — still very short compared to typical detector dimensions.

In the OGL viewer, fission fragments appear as very short blue stubs at the foil face. This is physically correct behavior, not a bug.

---

### 3. Prompt Fission Neutrons

**Source stage.** Production mechanism: the nascent fragments immediately after scission are highly excited (excitation energy of ~20–30 MeV each) and evaporate neutrons on a timescale of ~10⁻¹⁴ s — before any gamma emission, before any beta decay. These are *prompt* neutrons, emitted isotropically in the rest frame of each fragment but boosted into the lab frame by the fragment's velocity (~1.4% of c for the light fragment). The result is a slight forward-backward asymmetry in the lab, but no strong directionality.

#### 3.1 Multiplicity

The prompt neutron multiplicity $\nu$ follows a near-Poisson distribution. For ²³⁵U + thermal neutron:

- **Mean prompt multiplicity:** $\bar{\nu}_p = 2.4198$ (ENDF/B-VIII.0)
- **Mean delayed multiplicity:** $\bar{\nu}_d = 0.0157$
- **Total mean:** $\bar{\nu} = 2.4355$

The probability distribution $P(\nu)$ (fraction of fissions producing exactly $\nu$ prompt neutrons):

| $\nu$ | $P(\nu)$ |
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

$$\chi(E) = C \cdot e^{-E/a} \cdot \sinh\!\left(\sqrt{bE}\right)$$

where $C$ is a normalization constant and the ENDF/B-VII parameters for ²³⁵U are $a = 0.988\,\text{MeV}$ and $b = 2.249\,\text{MeV}^{-1}$.

Key spectral properties:

| Property | Value |
|---|---|
| Most probable energy | ~0.7 MeV |
| Mean energy | ~2.0 MeV |
| Median energy | ~1.3 MeV |
| Practical upper limit | ~12 MeV |
| Total KE (all prompt n) | $\bar{\nu} \times \bar{E} \approx 2.43 \times 2.0 \approx 4.8\,\text{MeV}$ |

The spectrum rises steeply from zero (the sinh factor), peaks around 0.7 MeV, and falls exponentially above the peak. Very few neutrons exceed 8–10 MeV. The shape arises from the Maxwellian-like evaporation spectrum of each fragment, convolved with the fragment velocity distribution.

This is the spectrum that Geant4's HP package samples when `G4ParticleHPManager::SetProduceFissionFragments(true)` is set and `G4HadronPhysicsQGSP_BIC_HP` is registered. The G4NDL data tables (derived from ENDF/B-VII) are used directly.

#### 3.3 Fate of Prompt Neutrons in This Geometry

With $\bar{\nu} \approx 2.43$ fast neutrons born per fission at ~2 MeV average:
- In an infinite ²³⁵U medium they would sustain a chain reaction ($k_\text{eff} = 1$ with sufficient mass).
- In this simulation the foil is 0.5 µm thick in a 20 cm air world — the neutrons escape into air immediately and reach the world boundary. Essentially none are re-captured in the foil on the timescale of a single event.
- In the OGL viewer, prompt neutrons appear as **green tracks** extending to the world boundary.
- When a plastic scintillator is added, fast neutrons can scatter off hydrogen nuclei (proton recoil), producing additional ionization. This is the basis of fast-neutron detection in organic scintillators.

---

### 4. Prompt Fission Gammas

**Source stage.** Production mechanism: after the fragments have evaporated their prompt neutrons (reducing their excitation energy by ~1–2 MeV per neutron), they are still highly excited and de-excite via statistical gamma emission on a timescale of ~10⁻¹⁰ s. These are the prompt fission gammas. They are distinct from the delayed gammas that accompany the subsequent β-decay chain.

#### 4.1 Multiplicity

- **Mean number of prompt gammas:** $\bar{N}_\gamma \approx$ **7–8 photons per fission**
- **Total prompt gamma energy:** $\bar{E}_\text{tot} \approx$ **7 MeV per fission**
- **Mean photon energy:** $\bar{E}_\gamma \approx 1$ MeV

The multiplicity fluctuates significantly event-by-event, anti-correlated with the neutron multiplicity (more neutrons → less excitation energy remaining → fewer gammas).

#### 4.2 Energy Spectrum

The prompt gamma spectrum for ²³⁵U thermal fission spans roughly 0.1 to 7 MeV. The differential yield (photons per MeV per fission) is approximately:

- **Below 1 MeV:** relatively flat, moderate yield — soft gammas from collective transitions and low-energy statistical de-excitation.
- **1–3 MeV:** falls roughly exponentially. This is the dominant energetic region; most of the total energy is carried by gammas in the 0.5–3 MeV band.
- **Above 3 MeV:** falls steeply. Hard gammas (> 4 MeV) arise from giant dipole resonance (GDR) transitions during the deformation phase near scission, but their yield is small.
- **Maximum observed:** ~8 MeV.

A commonly used empirical parameterization for the spectral shape above ~0.5 MeV:

$$\frac{dN}{dE} \propto \exp\!\left(-\frac{E}{E_0}\right), \qquad E_0 \approx 0.8\,\text{MeV}$$

#### 4.3 Timing and Source

Prompt gammas are emitted in two broad stages:

1. **Scission gammas** (bremsstrahlung-like, during neck rupture): very short timescale (~10⁻²⁰ s), contribute a small fraction of the total yield; the GDR gammas fall here.
2. **Fragment de-excitation gammas** (statistical evaporation): emitted after prompt neutron emission, ~10⁻¹⁴ to 10⁻¹⁰ s; this is the dominant source.

On any experimental timescale both contributions are instantaneous — the prompt gamma burst coincides with the fission event itself.

In the OGL viewer, prompt gammas (along with prompt neutrons) appear as **green tracks** extending to the world boundary.

---

### 5. Fission Fragment Decay Chains

**Source stage** (except where a decay product enters the scintillator — that boundary-crossing is Source→Detector and will be called out in Part II).

Both fission fragments land far from the valley of beta stability. Fragments in the light peak ($A \sim 90$–$100$) typically have 8–12 more neutrons than the stable isobar; fragments in the heavy peak ($A \sim 130$–$145$) have 6–10 excess neutrons. Each fragment undergoes a chain of β⁻ decays — emitting an electron and an antineutrino at each step — until it reaches a stable nuclide. Along the way, excited daughter states emit gamma rays, and a small fraction of decays produce delayed neutrons.

The average chain length is **3–4 β-decay steps per fragment**, or roughly **6–8 total per fission event**. Chain lengths range from 2 (for fragments that land close to stability) to 8+ (for the most neutron-rich primary fragments).

---

#### 5.1 Beta Electrons (β⁻)

**Production:** each β⁻ decay converts a neutron into a proton in the daughter nucleus, emitting an electron and an electron antineutrino. The $Q$-value of the decay is shared statistically between the electron and the antineutrino.

**Multiplicity:** ~6 β⁻ electrons per fission event on average (sum over both fragment chains).

**Energy spectrum:** each individual β⁻ has a continuous spectrum from zero to the endpoint $Q_\beta$, with the shape governed by phase space and the Fermi function:

$$\frac{dN}{dE} \propto p \cdot E \cdot (Q_\beta - E)^2 \cdot F(Z', E)$$

where $p$ is the electron momentum, $F(Z', E)$ is the Fermi function for the daughter nucleus of charge $Z'$, and $(Q_\beta - E)^2$ is the phase space factor for the antineutrino. The mean electron energy is approximately one-third of the endpoint: $\bar{E}_e \approx Q_\beta/3$.

Endpoint energies $Q_\beta$ for fission products span a wide range:

| Chain position | Typical $Q_\beta$ range | Example isotope |
|---|---|---|
| First generation (most neutron-rich) | 5–10 MeV | ⁹⁴Rb → ⁹⁴Sr: $Q_\beta = 9.9$ MeV |
| Middle of chain | 2–5 MeV | ⁹⁴Sr → ⁹⁴Y: $Q_\beta = 5.9$ MeV |
| Near stability | 0.5–2 MeV | ⁹⁴Y → ⁹⁴Zr: $Q_\beta = 4.9$ MeV |
| Long-lived (close to stable) | 0.1–0.5 MeV | ⁹⁰Sr → ⁹⁰Y: $Q_\beta = 0.546$ MeV |

Summed over all decays in both chains, the total β⁻ kinetic energy per fission is **~8 MeV** (with a comparable ~8 MeV going to antineutrinos, summing to the ~16 MeV total $Q_\beta$ averaged over all chains; exact totals depend on which fragments are produced).

**Timescale:** determined by the half-lives of the precursor isotopes, which span an enormous range:
- Short-lived precursors: milliseconds to seconds (e.g., ⁸⁷Br: $T_{1/2} = 55.7$ s, but daughters like ⁸⁷Kr: $T_{1/2} = 76$ min)
- Long-lived fission products: Cs-137 ($T_{1/2} = 30.2$ years), Sr-90 ($T_{1/2} = 28.8$ years)

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

| Decay | $\gamma$ energy | $T_{1/2}$ of parent | Why notable |
|---|---|---|---|
| ¹³⁷Cs → ¹³⁷ᵐBa → ¹³⁷Ba | 661.7 keV | 30.2 y → 2.55 min | The standard calibration line; dominant in spent-fuel gamma spectra |
| ¹³¹I → ¹³¹Xe | 364.5 keV | 8.02 d | Thyroid dose concern; used in nuclear medicine |
| ¹⁴⁰Ba → ¹⁴⁰La → ¹⁴⁰Ce | 1596 keV (La) | 12.75 d → 1.68 d | Strong line in fresh fission product mixtures |
| ⁹⁵Zr → ⁹⁵Nb → ⁹⁵Mo | 757 keV (Nb) | 64 d → 35 d | Prominent in medium-lived waste |

Delayed gammas are in principle detectable in the scintillator at any time after the fission event; for long-lived chains like Cs-137, the source persists essentially indefinitely on laboratory timescales.

---

#### 5.3 Delayed Neutrons

**Production:** a small fraction of β⁻ decays produce a *delayed neutron*. The mechanism is: the precursor nucleus (the neutron-rich fission product) undergoes β⁻ decay, leaving the daughter in a highly excited state with excitation energy *above the neutron separation energy* $S_n$ of the daughter. The daughter then promptly emits a neutron. Because the delay is set by the β⁻ decay half-life (not by the fast neutron-emission step), these neutrons appear delayed relative to fission.

**Total yield:**
- Delayed neutrons per fission: $\bar{\nu}_d =$ **0.0158** (ENDF/B-VIII.0)
- Total neutrons per fission: $\bar{\nu} = 2.4355$
- Delayed neutron fraction: $\beta = \bar{\nu}_d / \bar{\nu} =$ **0.0065 (0.65%)**

Despite the tiny fraction, delayed neutrons are what make nuclear reactors controllable: they extend the effective neutron lifetime from ~10⁻⁵ s (prompt alone) to ~0.1 s, allowing mechanical control systems to respond.

**Six-group representation** (Keepin group parameters for ²³⁵U + thermal fission):

| Group | $T_{1/2}$ (s) | Fraction of $\beta$ | Mean energy | Representative precursor |
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

**Production:** every β⁻ decay emits one electron antineutrino ($\bar{\nu}_e$), carrying away a share of the $Q$-value statistically determined by the same phase-space distribution as the electron.

**Multiplicity:** **~6 antineutrinos per fission** (one per β⁻ decay step, ~6 decays on average).

**Total energy:** **~12 MeV per fission** (mean ~2 MeV per antineutrino).

**Detectability:** antineutrinos interact only via the weak force. The inverse beta decay cross section (the most favorable detection reaction: $\bar{\nu}_e + p \rightarrow e^+ + n$) is ${\sim}10^{-41}\,\text{cm}^2$ at a few MeV. They escape every realistic detector with probability ~1 and are undetectable in this simulation.

Geant4's `G4RadioactiveDecayPhysics` does produce antineutrino tracks internally, but they are immediately killed (zero mean free path in the default configuration) and deposit no energy. They do not appear in the OGL viewer and do not affect any scoring.

The ~12 MeV they carry away is therefore irretrievably lost, which is why the *recoverable* energy per fission is ~190–195 MeV rather than the full ~202 MeV $Q$-value.

---

## Part II — Detector Stage: Scintillator Response

The Phase A implementation places the EJ-309 organic and LaBr$_3$(Ce) inorganic scintillator arrays and attaches full `G4MaterialPropertiesTables` to both materials, including per-particle scintillation yield curves. The processes documented below are now active in the simulation: a charged particle entering one of the scintillator volumes deposits energy via ionization, and `G4Scintillation::PostStepDoIt` produces optical photons whose number is set by the per-particle yield curve and whose time profile is set by the fast/slow time constants.

Optical photons are *generated* by Geant4 but **not yet scored** — there is no photocathode / SiPM model in Phase A. The infrastructure is complete enough that flipping on photon scoring is a one-line change (see `doc/architecture.md` "Optical infrastructure (deferred)"). Sections 6–8 below describe the physics that underpins those values.

**Boundary-crossing note (Source ↔ Detector):** the β$^-$ electrons and delayed γ's documented in Part I §5 are *Source-stage* particles when they're being produced inside the foil, but they cross into the Detector stage the moment they enter a scintillator volume and start depositing energy via the mechanisms below. In Geant4's track stack this is just one continuous tracking step — no special handling is required — but conceptually it is where Source and Detector physics meet, and it is the basis of the delayed-gamma spectroscopy goals in `design.md` §3.

---

### 6. Organic Scintillator Response — EJ-309

**Detector stage.** The EJ-309 array is the workhorse for fast-neutron detection (via proton recoil) and for prompt-γ detection. EJ-309 is a xylene-based liquid scintillator with proprietary fluors and wavelength-shifters; for simulation purposes the H/C composition (H 9.5 %, C 90.5 % by mass at 0.959 g/cm$^3$) is the load-bearing detail.

#### 6.1 Singlet–Triplet Mechanism

Aromatic organic scintillators emit light through the de-excitation of $\pi$-electron states of the solvent's molecules. Two populations of excited states are relevant:

- **Singlet states** ($S_1$, total spin 0) decay rapidly via fluorescence to the ground state $S_0$, emitting a photon. Characteristic lifetime in EJ-309: $\tau_1 \approx 3.5$ ns. This is the **fast component**.
- **Triplet states** ($T_1$, total spin 1) cannot decay to $S_0$ via single-photon emission (spin-forbidden). They de-excite via *triplet–triplet annihilation* between two neighboring $T_1$ excitons:

$$T_1 + T_1 \;\rightarrow\; S_1^* + S_0$$

producing one excited singlet that subsequently fluoresces. Because this requires two triplets to encounter each other, the effective lifetime depends on triplet density and is much longer: $\tau_2 \approx 32$ ns in EJ-309. This is the **slow component**.

The total scintillation light is therefore a sum of two exponentials:

$$\frac{dN_\gamma}{dt} \;=\; \frac{Y_1 N_\gamma^{\text{tot}}}{\tau_1} e^{-t/\tau_1} \;+\; \frac{Y_2 N_\gamma^{\text{tot}}}{\tau_2} e^{-t/\tau_2}$$

where $Y_1, Y_2$ are the fast and slow yield fractions ($Y_1 + Y_2 = 1$) and $N_\gamma^{\text{tot}}$ is the total photon count.

#### 6.2 Why $Y_1, Y_2$ Depend on Particle Type

The relative population of singlets vs. triplets at the moment of excitation depends on the *linear energy transfer* (LET) of the incident particle, $-dE/dx$:

- **Minimum-ionizing electrons** ($\sim$ 1–2 MeV/(g/cm$^2$) in plastic) excite a relatively dilute column of states. Most are formed as singlets (statistical 1:3 ratio is overcome by selection rules favoring $S_1$); few triplets, so the fast component dominates: in EJ-309, $Y_1 \approx 0.85$, $Y_2 \approx 0.15$ for electrons.
- **Recoil protons** from $n + p$ elastic scattering (the dominant fast-neutron detection channel in organic scintillators) deposit energy at much higher LET — tens to hundreds of MeV/(g/cm$^2$) at MeV energies. The dense ionization column produces both $S_1$ and $T_1$ populations more efficiently, but more importantly the high triplet density allows triplet–triplet annihilation to convert triplets into delayed singlets that emit on the slow timescale. Net result: a fatter slow tail. In EJ-309 we use $Y_1 \approx 0.55$, $Y_2 \approx 0.45$ for protons.
- **Heavy ions** (fission fragments, alphas) have even higher LET and produce a still-fatter slow tail: $Y_1 \approx 0.40$, $Y_2 \approx 0.60$.

These are the values populated as `ELECTRONSCINTILLATIONYIELD1/2`, `PROTONSCINTILLATIONYIELD1/2`, `IONSCINTILLATIONYIELD1/2`, and `ALPHASCINTILLATIONYIELD1/2` on the EJ-309 `G4MaterialPropertiesTable`. Citation: F. D. Brooks, *Nucl. Instrum. Meth.* 4, 151 (1959).

#### 6.3 Birks' Law and Light-Output Curves

The total photon yield $N_\gamma^{\text{tot}}$ is *not* linear in deposited energy at high LET, because the same dense ionization column that boosts the slow fraction also quenches some of the available excitation via non-radiative pathways. The standard parameterization is **Birks' formula**:

$$\frac{dN_\gamma}{dx} \;=\; \frac{S \cdot dE/dx}{1 \,+\, kB \cdot dE/dx}$$

where $S$ is the absolute light-yield constant for the material (in photons per unit deposited energy in the linear limit) and $kB$ is the Birks constant (in mm/MeV). For low-LET particles $kB \cdot dE/dx \ll 1$ and the response is linear ($dN_\gamma/dx \approx S \cdot dE/dx$); for high-LET particles $kB \cdot dE/dx \gg 1$ and the response *saturates* at $S/kB$.

For EJ-309 we use:

- $S = 12{,}300$ photons / MeV (electron-equivalent absolute yield)
- $kB \approx 0.11$ mm/MeV (Eljen datasheet; close to the Brooks-fitted value for similar PVT-based scintillators)

Integrating Birks' law over a particle's track gives the **light-output curve** $L_p(E_p)$ — the number of scintillation photons (or equivalently, MeVee of "electron-equivalent" deposited energy) produced by a particle of initial kinetic energy $E_p$ slowing to rest in the material. For protons in EJ-309 (Enqvist 2013 fit), representative values:

| $E_p$ (MeV) | $L_p$ (photons) | $L_p / E_p$ (MeVee/MeV) |
|---|---|---|
| 0.5 | 490 | 0.04 |
| 1.0 | 1{,}970 | 0.16 |
| 2.0 | 6{,}150 | 0.25 |
| 5.0 | 25{,}800 | 0.42 |
| 10.0 | 68{,}900 | 0.56 |
| 20.0 | 172{,}000 | 0.70 |

The strong sub-linearity at low $E_p$ — a 1 MeV proton produces only 16 % of the light a 1 MeV electron would — is the fingerprint of Birks quenching and is what makes proton-recoil PSD work: even when the *total* light yield matches a gamma's, the proton's slow-fraction $Y_2$ is much larger.

These tabulated values are attached as the `PROTONSCINTILLATIONYIELD` curve on the EJ-309 MPT (cumulative form: yield from 0 to $E_p$, so per-step photon counts come out as differences). The corresponding heavy-ion and alpha curves are even more strongly quenched.

When `G4OpticalParameters::SetScintByParticleType(true)` is set, Geant4 uses these per-particle curves *directly* and bypasses the unified Birks-saturation pathway. The `SetBirksConstant` call on the EJ-309 material is therefore redundant for scintillation light generation in this configuration (it remains relevant for any code path that asks for visible energy via `G4EmSaturation`). Geant4 emits the informational `Scint02` warning *"Birks Saturation is replaced by ScintillationByParticleType"* at startup to confirm this branch is active.

Citations: Brooks, NIM 4 (1959) 151; A. Enqvist *et al.*, *Nucl. Instrum. Meth.* A 715, 79 (2013); S. A. Pôzzi *et al.*, on the EJ-301/NE-213 series.

---

### 7. Inorganic Scintillator Response — LaBr$_3$(Ce)

**Detector stage.** The LaBr$_3$(Ce) detectors at backward angles are the gamma-spectroscopy arm of the system: they exploit the high effective Z of the lattice ($Z_{\text{eff}} \approx 46$) for efficient photopeak detection, and the very narrow intrinsic resolution (~2.8 % FWHM at 662 keV) for line identification.

#### 7.1 Activator-Mediated Mechanism

Unlike organic scintillators, where light arises from de-excitation of solvent molecular states, inorganic scintillators emit through a **doped-activator** mechanism in a crystalline host:

1. Ionizing radiation creates electron–hole pairs in the LaBr$_3$ conduction/valence bands.
2. Charge carriers migrate through the lattice and are captured at Ce$^{3+}$ activator sites.
3. The Ce activator is excited to the $5d$ configuration: Ce$^{3+}$ + $e^-$ + hole $\rightarrow$ Ce$^{3+*}(5d)$.
4. The excited activator de-excites via the dipole-allowed $5d \rightarrow 4f$ transition, emitting a photon at $\sim 380$ nm.

This pathway has a **single intrinsic decay constant** — the $5d$ lifetime, $\tau \approx 16$ ns — with no slow-component analog of the organic singlet–triplet system. As a result, **LaBr$_3$ has no PSD capability**: the pulse shape is invariant under particle type (only the total light yield differs).

In the LaBr$_3$ MPT we therefore set only `SCINTILLATIONCOMPONENT1`, `SCINTILLATIONTIMECONSTANT1 = 16 ns`, and `SCINTILLATIONYIELD1 = 1.0` — no second component, no $Y_1/Y_2$ split.

#### 7.2 Light Yield and Quenching

Total absolute yield: $S = 63{,}000$ photons / MeV — about $5\times$ that of EJ-309. Combined with the high Z, this is what drives LaBr$_3$'s photopeak resolution.

The Birks constant for inorganic scintillators is much smaller than for organics because the lattice de-excitation pathway is less sensitive to local LET density:

$$kB_{\text{LaBr}_3} \approx 1.3 \times 10^{-3} \text{ mm/MeV}$$

(Moszynski 2006). For electrons at MeV scale, $kB \cdot dE/dx \ll 1$ and the response is essentially **linear in deposited energy**.

For heavy charged particles the response is nonlinear, but the deviation is described empirically by the so-called *alpha–beta ratio*, the ratio of light output for an alpha to that of an electron at the same kinetic energy. For LaBr$_3$(Ce), $\alpha/\beta \approx 0.3$ at MeV energies. In Phase A the LaBr$_3$ per-particle yield curves are kept linear (same as electrons) because no photon-level scoring is yet active and the simplification is harmless; refinement to actual $\alpha/\beta < 1$ curves is a later-pass detail flagged in `design.md` §3 and `architecture.md` Intrinsic-background TODO context.

#### 7.3 Energy Resolution and the Photon-Counting Limit

The intrinsic energy resolution of LaBr$_3$(Ce) is dominated by Poisson statistics on the number of detected scintillation photons:

$$\frac{\sigma_E}{E} \;\propto\; \frac{1}{\sqrt{N_\gamma}}$$

For $E = 662$ keV (the standard $^{137}$Cs calibration line) the total number of photons produced is $N_\gamma = 0.662 \times 63{,}000 \approx 41{,}500$. After photodetector quantum efficiency, light collection, and intrinsic non-proportionality, an experimentally observed FWHM of $\sim$2.8 % is achievable, which is among the best of any commercially available inorganic scintillator. PMTs and SiPMs typically deliver ~30–40 % photon detection efficiency at the 380 nm emission wavelength.

This resolution is what enables identification of specific fission-product gamma lines (e.g., $^{140}$La at 1596 keV, $^{95}$Zr at 757 keV, $^{137}$Cs at 662 keV). **In Phase A this is documented but not yet scored** — there is no photodetector model and `hits.csv` records only the energy deposited per step, not the smeared photon count.

---

### 8. Pulse Shape Discrimination (PSD)

**Detector stage.** PSD is the technique used to distinguish neutron events (proton recoils in the EJ-309) from gamma events (Compton electrons in the EJ-309) on a per-pulse basis. It uses *only the shape* of the scintillation pulse from a single detector — no external timing reference is required, no second detector, no coincidence.

This is the central distinction between PSD and **time-of-flight (TOF)**: TOF measures particle *energy* by timing arrival relative to a fission-instant reference (in a real experiment, a trigger scintillator coupled to the foil; in simulation, the Geant4 fission vertex time). PSD identifies particle *type*. The two are complementary: PSD says "this is a neutron"; TOF says "this neutron has $E_n =$ such-and-such".

#### 8.1 Tail-to-Total Ratio

The standard PSD figure of merit is the **tail-to-total integral ratio**:

$$\text{PSD} \;=\; \frac{Q_{\text{tail}}}{Q_{\text{total}}} \;=\; \frac{\int_{t_0 + t_d}^{t_0 + T} V(t) \, dt}{\int_{t_0}^{t_0 + T} V(t) \, dt}$$

where:

- $V(t)$ is the digitizer-recorded voltage waveform (proportional to scintillation light);
- $t_0$ is the pulse start, identified by a leading-edge or constant-fraction discriminator on the rising edge — this is the *intrinsic* timing reference;
- $t_d$ is the tail-start delay, typically chosen at $\sim 30$ ns past $t_0$ so the fast component (3.5 ns time constant) has decayed below ~$e^{-9} \approx 0.01$% of its peak;
- $T$ is the integration window, typically a few hundred ns to capture most of the slow component.

Substituting the two-exponential pulse shape from §6.1, integrated from the pulse start:

$$Q_{\text{total}}(T) \;=\; N_\gamma^{\text{tot}} \left[ Y_1 (1 - e^{-T/\tau_1}) + Y_2 (1 - e^{-T/\tau_2}) \right]$$

$$Q_{\text{tail}}(t_d, T) \;=\; N_\gamma^{\text{tot}} \left[ Y_1 (e^{-t_d/\tau_1} - e^{-T/\tau_1}) + Y_2 (e^{-t_d/\tau_2} - e^{-T/\tau_2}) \right]$$

In the limits $\tau_1 \ll t_d \ll \tau_2 \ll T$ (a good approximation for EJ-309's 3.5 ns / 32 ns / typical 30 ns / typical 400 ns), the fast contribution to $Q_{\text{tail}}$ vanishes and the slow contribution to $Q_{\text{total}}$ saturates, giving the simple analytic limit:

$$\frac{Q_{\text{tail}}}{Q_{\text{total}}} \;\approx\; \frac{Y_2}{Y_1 + Y_2} \;=\; Y_2$$

(since we normalize $Y_1 + Y_2 = 1$). For EJ-309 with the values from §6.2 this gives:

| Particle | $Y_2$ | Expected PSD ratio |
|---|---|---|
| Electron / γ | 0.15 | $\sim 0.15$ |
| Proton (recoil from neutron) | 0.45 | $\sim 0.45$ |
| Alpha / heavy ion | 0.60 | $\sim 0.60$ |

In the experimentally observed scatter plot of $Q_{\text{tail}}/Q_{\text{total}}$ vs. $Q_{\text{total}}$ (or equivalently vs. light output), gamma events form a narrow band near 0.15 and neutron events form a narrow band near 0.45 — well-separated, with overlap only at very low light output where the photon-counting Poisson noise smears the bands together. The "figure of merit" of a PSD measurement is the band separation in units of the bands' widths.

#### 8.2 What PSD Does Not Do

It is worth being explicit about what PSD does **not** measure, because PSD and TOF are routinely confused in casual discussion:

- It does **not** measure neutron kinetic energy. That comes from TOF: $E_n = \frac{1}{2} m_n (d/t)^2$ where $d$ is the flight path and $t$ is the time between fission and detector hit.
- It does **not** require a coincidence with another detector or with a trigger. The pulse-start discriminator on the EJ-309 channel itself provides the timing reference for the integration windows.
- It does **not** require a fission trigger scintillator. In simulation the fission instant is known exactly from the Geant4 vertex; in a real experiment, you'd need a trigger for TOF (and `design.md` §1.C reserves that role), but PSD alone runs on a single EJ-309 channel.

#### 8.3 Phase A Status

Phase A populates all the inputs PSD needs — the fast/slow time constants, the per-particle $Y_1/Y_2$ ratios, and the light-output curves — on the EJ-309 `G4MaterialPropertiesTable`. Optical photons are *generated* by `G4Scintillation::PostStepDoIt` whenever a charged particle deposits energy in the EJ-309 volume, and they propagate (refraction at boundaries via RINDEX, attenuation via ABSLENGTH) through the geometry. They are **not yet scored**: there is no photocathode surface, no waveform digitizer, no $Q_{\text{tail}}/Q_{\text{total}}$ extraction. The `hits.csv` produced by Phase B records the *ground-truth* energy depositions and entry times — the inputs from which a Phase D PSD pipeline could later synthesize digitizer waveforms.

---

### 9. Optical Photon Transport (deferred scoring)

For completeness, the optical-photon physics that *is* active in Phase A:

- **Refractive index** sets boundary refraction at scintillator–air interfaces. Set to 1.57 for EJ-309 and 1.9 for LaBr$_3$(Ce) — flat across the photon-energy grid because the materials' published dispersion is small over the visible band.
- **Bulk absorption length** governs how far photons propagate before being absorbed in the bulk material. Set to 100 cm for EJ-309 and 50 cm for LaBr$_3$ — order-of-magnitude placeholders pending wavelength-resolved data. Until photon scoring is enabled this value is consequence-free; once it is, refining `ABSLENGTH` matters for predicting light-collection efficiency.
- **Emission spectrum** (`SCINTILLATIONCOMPONENT1`/`COMPONENT2`) is the spectral distribution of the scintillation photon energies. Coarse Gaussian-like sampling around the 424 nm (EJ-309) and 380 nm (LaBr$_3$) peaks is currently used — adequate when photons aren't wavelength-tagged at scoring.
- **No Cherenkov radiation**: not configured in the materials (no `RINDEX` curve covering the high-energy region where charged particles would exceed the speed of light in the medium). For our energy regime — fission fragments (β $\approx$ 0.05) and MeV-scale electrons (β $\approx$ 0.94 vs. $1/n \approx 0.64$ in EJ-309) — Cherenkov light from electrons could in principle contribute a few photons but is dwarfed by scintillation; turning Cherenkov off in `G4OpticalPhysics` is appropriate for a scintillation-dominated detector.

When Phase B+ enables photon scoring, the natural addition to the pipeline is:

1. A `G4OpticalSurface` on the inner face of each EJ-309 housing (specular reflectance for the polished aluminum case, or a wrapped-Tyvek diffuser model — to be chosen per the photodetector's optical assembly).
2. A photocathode logical volume at the back of each scintillator with a thin sensitive layer; the Phase B `ScintillatorSD::ProcessHits` short-circuit on `G4OpticalPhoton` is then removed, and a separate SD on the photocathode counts arrivals.
3. A SiPM / PMT response model in offline analysis applied to the recorded photon arrival times — PDE, dark count rate, crosstalk, afterpulsing — to convert ground-truth photon arrivals into realistic digitizer waveforms.

The MPT entries already in place do not need to be revisited for any of this — the per-particle yield curves and time constants are the inputs all photon-level analyses will consume.
