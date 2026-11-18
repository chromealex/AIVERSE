// AIVerse InputFlow — genre-neutral, action-driven input for native C++ games.
//
// Canonical public header and namespace for the successor to Enhanced Input.
// Old includes, namespaces and InputSystem remain compatible.
// This is presentation-only; deterministic ECS receives validated Commands.
#pragma once

#include "EnhancedInput.h"

namespace aiverse::lib::gameplay::inputflow {
using enhanced::Action;
using enhanced::ActionEvent;
using enhanced::ActionState;
using enhanced::BindingConflict;
using enhanced::Control;
using enhanced::Device;
using enhanced::InputFlow;
using enhanced::InputSystem;  // compatibility name
using enhanced::Mapping;
using enhanced::MappingContext;
using enhanced::Modifier;
using enhanced::ModifierKind;
using enhanced::Phase;
using enhanced::RebindPolicy;
using enhanced::Trigger;
using enhanced::TriggerKind;
using enhanced::Value;
using enhanced::ValueType;
using enhanced::applyModifiers;

// More descriptive type names for newly generated game and tool code.
using InputAction = enhanced::Action;
using InputContext = enhanced::MappingContext;
using InputValue = enhanced::Value;
using InputActionEvent = enhanced::ActionEvent;
using InputActionState = enhanced::ActionState;
} // namespace aiverse::lib::gameplay::inputflow
