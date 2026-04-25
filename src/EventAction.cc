#include "EventAction.hh"

MyEventAction::MyEventAction(MySteppingAction* steppingAction)
    : fSteppingAction(steppingAction)
{}

MyEventAction::~MyEventAction() {}