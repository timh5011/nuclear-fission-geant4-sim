#include "EventAction.hh"

MyEventAction::MyEventAction(MySteppingAction* steppingAction, MySensitiveDetector* sensitiveDetector)
    : fSteppingAction(steppingAction), fSensitiveDetector(sensitiveDetector)
{}

MyEventAction::~MyEventAction() {}