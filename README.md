# GEANT4 Simulation of Thermal Neutron-Induced U-235 Fission with Plastic Scintillator Readout

log confirms it:
                                                                                                                                                                                                        
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
                                                                                      