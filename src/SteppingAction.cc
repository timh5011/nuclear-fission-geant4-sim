#include "SteppingAction.hh"

// Need to identify the difference in DAQ uses between Stepping Action and Sensitive Detector.

MySteppingAction::MySteppingAction()
    : totalOpticalPhotonEnergy(0.0), totalLightYield(0.0), totalDepositedEnergy(0.0)
{}

MySteppingAction::~MySteppingAction() {}