#include "Action.hh"

MyActionInitialization::MyActionInitialization()  = default;
MyActionInitialization::~MyActionInitialization() = default;

// -----------------------------------------------------------------------------
// Build — instantiate the four user actions and register them with the kernel.
//
// Construction order matters for one reason: MyEventAction's constructor
// takes a MyRunAction* (so EndOfEventAction can reach the EventWriter). So
// RunAction must exist before EventAction. SetUserAction order is
// independent — Geant4 dispatches each action to the right kernel hook
// regardless of the registration order.
// -----------------------------------------------------------------------------
void MyActionInitialization::Build() const {
    auto* runAction      = new MyRunAction();
    auto* generator      = new MyPrimaryGenerator();
    auto* steppingAction = new MySteppingAction();
    auto* eventAction    = new MyEventAction(steppingAction, runAction);

    SetUserAction(runAction);
    SetUserAction(generator);
    SetUserAction(steppingAction);
    SetUserAction(eventAction);
}
