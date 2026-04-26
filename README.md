# GEANT4 Simulation of Thermal Neutron-Induced U-235 Fission with Plastic Scintillator Readout

Note: If modifying the simulation (re-running cmake), must run

``` source /path/to/geant4-v11.4.0-install/bin/geant4.sh ```

---------------------------------------

log confirms:
                                                                                                                                                                                                        
  Produce fission fragments       1                                                                                                                                                                     
  Fission fragment production is now activated in HP package for Z = 92, A = 235                                                                                                                        
                                                                                                                                                                                                        
  Here's what you're seeing in the image:                                                                                                                                                               
                                                            
  - Green tracks — neutral particles: prompt fission neutrons (2–3 per event) and prompt gammas. They travel long distances because they have no charge and don't ionize, so they sail straight to the  
  world boundary.
  - Blue stubs right at the foil — the fission fragments (e.g. Ba-141 and Kr-92). They are highly positively charged heavy ions, so they stop extremely quickly — within a few micrometers of the foil —
   which is why they appear as tiny stubs barely visible at the vertex. This is physically correct.                                                                                                     
  - Red tracks — negative particles, specifically electrons (beta particles from the radioactive decay chains of the fission fragments). These travel further than the fragments but curve/scatter more
  than the neutral tracks.                                                                                                                                                                              
                                                            
  The simulation is doing exactly what it should: a thermal neutron enters the uranium foil, fission occurs, and you get the full secondary shower. The asymmetry you see (more tracks going one        
  direction) is just the random momentum conservation of one particular fission event.
                                                                                      
---------------------------------------

what do you mean by this: One caveat worth flagging: pure ²³⁵U at 19.1 g/cm³ in cubic-cm volumes is unphysical (you'd be near critical mass at ~52 kg of bare metal). For a sub-cm cube you're fine,  
  but don't scale the cube up                                                                                                                                                                           
     much past that without thinking about it.                                                                                                                                                          
                                                                                                                                                                                                        
⏺ Two separate things bundled into that warning — let me unpack them.                                                                                                                                   
                                                                                                                                                                                                      
  1. What "critical mass" means here
                                                                                                                                                                                                        
  A bare sphere of pure ²³⁵U at 19.1 g/cm³ goes critical (k_eff = 1, self-sustaining chain reaction) at about 47–52 kg, which is a sphere of radius ~8.6 cm or a cube ~17 cm on a side. Above that,     
  every fission produces on average more than one fission in the next generation → exponential growth. Below it, neutrons leak out faster than they can cause new fissions. (For reference: Little Boy  
  used ~64 kg of HEU.)                                                                                                                                                                                  
                  
  Your current world is a 20 cm cube (10 cm half-side), so you couldn't even fit a critical mass in the world volume — but you could get uncomfortably close to it (e.g. a 5 cm cube of pure ²³⁵U is    
  ~2.4 kg, well below critical, but a 10 cm cube is ~19 kg, getting noticeable neutron multiplication even though still subcritical).
                                                                                                                                                                                                        
  2. Why this matters for Geant4 specifically                                                                                                                                                           
   
  Geant4's HP package is a radiation transport code, not a criticality code. Specialized criticality codes (MCNP-K, KENO, Serpent) use k-eigenvalue iteration and fission-source convergence algorithms 
  that Geant4 does not implement. Geant4 just does analog Monte Carlo: it tracks every secondary neutron from every fission until it escapes, gets captured, or falls below threshold.
                                                                                                                                                                                                        
  Two practical consequences if you make the target large enough that secondaries cause more fissions:                                                                                                  
   
  - Performance. Each prompt fission neutron that fissions again produces ~2.4 more neutrons. If your geometry has k_eff close to 1, a single primary can trigger thousands of secondary fissions within
   one event — runtime per event blows up, memory blows up, the OGL viewer chokes on track storage. With k_eff > 1, runtime per event is unbounded.
  - Validity. Geant4 wasn't validated for this regime. Subtle issues (delayed-neutron treatment, fission spectrum sampling, multi-generation tally bias) that don't matter for sub-mfp foils start      
  mattering. Results may look reasonable and be wrong.                                                                                                                                                  
   
  3. Where the safe zone is                                                                                                                                                                             
                  
  - Sub-cm cubes (e.g. 5 mm cube, 5 × 5 × 1 mm slab): mass ~tens to hundreds of mg, k_eff effectively 0, every primary causes at most ~1 fission, secondaries leak. This is the regime where Geant4 is  
  doing what it's good at.
  - ~1 cm cube (~19 g): still very subcritical, but you'll start seeing the occasional secondary fission inside the bulk. Still fine, just not single-interaction-clean anymore.                        
  - Several cm cubes: starting to enter "you're modeling a small fission assembly" territory — runtime grows, results need to be interpreted carefully, and you're past what this code was set up to do.
                                                                                                                                                                                                        
  So the practical recommendation: if you go to a cube, keep it ≤ a few mm on a side. That's plenty of mfp for ~100% interaction probability per primary, but small enough that secondary neutrons leak 
  instead of multiplying, which keeps each event clean and Geant4 in its validated regime.

  ----
  Along the beam (200 µm):                                                                                                                                                                              
  - 200 µm / 292 µm mfp = 0.69 mfp → P(interact) ≈ 50%, P(fission) ≈ 41%
  - So not quite "consistent" — roughly 2-in-5 events fission. For >75%, you'd need ~400 µm.                                                                                                            
                  
  Transverse (1 µm):                                                                                                                                                                                    
  - Fragment range in uranium ≈ 8 µm, transverse half-width = 0.5 µm
  - Fragment escapes through the side wall if sin(θ) × 8 µm > 0.5 µm → θ > 3.6°                                                                                                                         
  - Any fragment emitted more than 3.6° from the beam axis escapes → ~99.8% of fragments escape through the sides
                                                                                                                                                                                                        
  The key new physics this unlocks: once a fragment exits the uranium into air, its range scales inversely with density:                                                                                
                                                                                                                                                                                                        
  range_air ≈ 8 µm × (19.1 g/cm³ / 0.0013 g/cm³) ≈ 12 cm                                                                                                                                                
                                                                                                                                                                                                        
  So fragments that were previously invisible (stopping in µm of dense U) now travel ~12 cm in air before stopping. You'll see prominent blue tracks radiating transversely from the target — exactly   
  what makes fission visually striking.                                                                                                                                                                 
                                                                                                                                                                                                        
  One practical concern: Geant4's default production cuts are 1 mm, and your transverse is 1 µm. Secondaries produced inside the target won't be individually tracked below threshold. For prompt       
  neutrons and gammas this doesn't matter much, but it's cleaner to use 10 µm transverse instead — fragments still escape (~78% escape through sides) and you avoid potential step-size issues.
                                                                                                                                                                                                        
  Recommended geometry: 500 µm × 10 µm × 10 µm along x̂                                                                                                                                                  
  - P(fission) ≈ 82% — genuinely consistent
  - ~78% of fragments escape transversely into air with ~12 cm of range                                                                                                                                 
  - No production-cut issues                                           