#include "Action.hh"

MyActionInitialization::MyActionInitialization()  = default;
MyActionInitialization::~MyActionInitialization() = default;

// -----------------------------------------------------------------------------
// Build — instantiate the five user actions and register them with the kernel.
//
// Construction order matters because MyEventAction's constructor takes
// pointers to MySteppingAction, MyTrackingAction, and MyRunAction (so
// EndOfEventAction can reach the writers and the buffered-flush methods).
// SetUserAction order is independent — Geant4 dispatches each action to the
// right kernel hook regardless of registration order.
// -----------------------------------------------------------------------------
void MyActionInitialization::Build() const {
    auto* runAction      = new MyRunAction();
    auto* generator      = new MyPrimaryGenerator();
    auto* steppingAction = new MySteppingAction();
    auto* trackingAction = new MyTrackingAction();
    auto* eventAction    = new MyEventAction(steppingAction,
                                             trackingAction,
                                             runAction);

    SetUserAction(runAction);
    SetUserAction(generator);
    SetUserAction(steppingAction);
    SetUserAction(trackingAction);
    SetUserAction(eventAction);
}
