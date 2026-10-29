#pragma once

#include "mealkit/core/Math.h"
#include "mealkit/core/Reflect.h"
#include "mealkit/core/Types.h"
#include "mealkit/platform/Gamepad.h"

#include <string>
#include <string_view>
#include <vector>

namespace mealkit {
class Input;
}

// Input action mapping (docs/CODING_MODEL.md "Input"). Game code doesn't look at keys and reads only action values:
//   ctx.actions.axis2("move"), ctx.actions.pressed("jump")
// Actions and bindings are in the engine table assets/input.bindings (row id = action name, editor [Tables] panel):
//   row move
//   kind Axis2
//   keys W S A D Up Down Left Right
//   pad LeftStick
//   row jump
//   keys Space
//   pad A
// Bindings changed by the player go in a file of the same format in the user settings folder (changed rows only), layered over the project file (unrelated to the content hash).
// Action values are used only on the view side; the game quantizes them into an 8-byte GameInput for the simulation.
namespace mealkit::input {

inline constexpr const char* kBindingsTable = "input.bindings"; // relative to assets

// Button: pressed. Axis: -1..1 (keys come in "negative positive" pairs). Axis2: length at most 1 (keys in fours: "up down left right").
// Delta, Delta2: change amount this frame (mouse pixels/wheel notches as is, keys and sticks rate × seconds) — camera rotation/zoom.
MEALKIT_ENUM(ActionKind, u8, Button, Axis, Axis2, Delta, Delta2);

// Field descriptions are editor text, so written in English with Korean in engine/res/strings/ko.txt
MEALKIT_STRUCT(Action) {
    MEALKIT_FIELD(ActionKind, kind, ActionKind::Button, "Kind (Button, Axis, Axis2, Delta, Delta2)");
    MEALKIT_FIELD(std::string, label, {}, "Name shown to players");
    MEALKIT_FIELD(std::string, keys, {}, "Keys (Axis: negative positive, Axis2: up down left right)");
    MEALKIT_FIELD(std::string, mouse, {}, "Mouse (Left, Right, Middle, WheelUp, WheelDown, Wheel, Move, MoveX, MoveY)");
    MEALKIT_FIELD(std::string, pad, {}, "Gamepad (A, B, X, Y, LeftStick, RightStick, DPad, LeftTrigger ..., -name is negative)");
    MEALKIT_FIELD(f32, rate, 1.0f, "Delta: amount per second from keys and sticks");
    MEALKIT_FIELD(f32, deadzone, 0.2f, "Stick and trigger dead zone (0 to 1)");
};

// name ↔ code (names used by binding files and the settings screen). 0 for unknown names.
u8 keyFromName(std::string_view name);
const char* keyName(u8 vk); // nullptr for keys without a name

// Last used device (for matching button prompts to the device)
enum class Device : u8 { KeyboardMouse, Gamepad };

// The engine makes one per process (window) and passes it as GameContext::actions. The game only reads it.
class Actions {
public:
    Actions();
    ~Actions();
    Actions(const Actions&) = delete;
    Actions& operator=(const Actions&) = delete;

    // Binding file changed by the player (usually game::userFolder(settings) + "/input.bindings"). Empty string = none.
    void setUserFile(std::string path);
    // Reads assets/input.bindings and the user file. update checks file times every 0.25 s and rereads.
    void load();

    // Once per frame (engine, before game update). Devices not allowed are treated as pressing nothing.
    void update(const Input& input, const PadState& pad, bool keyboardAllowed, bool mouseAllowed, f64 dt);
    // All values released (when the window loses focus, when a match restarts)
    void clear();
    // Player setting sensitivity/Y-axis invert: multiplied into the mouse and gamepad parts of Delta2 actions (camera rotation) (before input quantization, view side only)
    void setLookSettings(f32 mouseScale, f32 padScale, bool invertMouseY, bool invertPadY);

    // Read by name. Missing actions are 0 and warn once per name.
    bool down(std::string_view action) const;
    bool pressed(std::string_view action) const;  // pressed this frame
    bool released(std::string_view action) const; // released this frame
    f32 axis(std::string_view action) const;      // Axis, Delta (Button is 0/1)
    Vec2 axis2(std::string_view action) const;    // Axis2, Delta2
    Device lastDevice() const;

    // Bindings (settings screen): current values (project + user), change, revert, save to the user file (only rows differing from the project)
    u32 count() const;
    const std::string& id(u32 index) const;
    const Action& binding(u32 index) const;
    bool setBinding(std::string_view action, const Action& binding);
    void resetBinding(std::string_view action);
    bool saveUser() const;

private:
    struct Impl;
    Impl* m;
};

// Engine tables shown in the editor [Tables] panel (bindings)
const reflect::TableList& engineTables();

} // namespace mealkit::input
