#pragma once

#include "mealkit/core/Math.h"
#include "mealkit/core/Reflect.h"
#include "mealkit/core/Types.h"

#include <string>
#include <string_view>
#include <vector>

namespace mealkit {
class Input;
}

// 입력 액션 매핑 (docs/CODING_MODEL.md "입력"). 게임 코드는 키를 보지 않고 액션 값만 읽는다:
//   ctx.actions.axis2("move"), ctx.actions.pressed("jump")
// 액션과 바인딩은 엔진 표 assets/input.bindings (행 id = 액션 이름, 에디터 [표] 패널):
//   row move
//   kind Axis2
//   keys W S A D Up Down Left Right
//   pad LeftStick
//   row jump
//   keys Space
//   pad A
// 플레이어가 바꾼 바인딩은 사용자 설정 폴더의 같은 형식 파일(바뀐 행만)에 두고 프로젝트 파일 위에 덮는다(콘텐츠 해시와 무관).
// 액션 값은 화면 쪽에서만 쓰고, 시뮬레이션에는 게임이 GameInput 8바이트로 양자화해 보낸다.
namespace mealkit::input {

inline constexpr const char* kBindingsTable = "input.bindings"; // assets 기준

// Button: 눌림. Axis: -1..1 (keys는 "음수 양수" 짝). Axis2: 길이 1 이하 (keys는 "위 아래 왼쪽 오른쪽" 넷씩).
// Delta, Delta2: 이번 프레임의 변화량 (마우스는 픽셀·휠 칸 그대로, 키와 스틱은 rate × 초) — 카메라 돌리기·확대.
MEALKIT_ENUM(ActionKind, u8, Button, Axis, Axis2, Delta, Delta2);

// 필드 설명은 에디터 글자라 영어로 쓰고 한국어는 engine/res/strings/ko.txt
MEALKIT_STRUCT(Action) {
    MEALKIT_FIELD(ActionKind, kind, ActionKind::Button, "Kind (Button, Axis, Axis2, Delta, Delta2)");
    MEALKIT_FIELD(std::string, label, {}, "Name shown to players");
    MEALKIT_FIELD(std::string, keys, {}, "Keys (Axis: negative positive, Axis2: up down left right)");
    MEALKIT_FIELD(std::string, mouse, {}, "Mouse (Left, Right, Middle, WheelUp, WheelDown, Wheel, Move, MoveX, MoveY)");
    MEALKIT_FIELD(std::string, pad, {}, "Gamepad (A, B, X, Y, LeftStick, RightStick, DPad, LeftTrigger ..., -name is negative)");
    MEALKIT_FIELD(f32, rate, 1.0f, "Delta: amount per second from keys and sticks");
    MEALKIT_FIELD(f32, deadzone, 0.2f, "Stick and trigger dead zone (0 to 1)");
};

// 게임패드 한 개의 상태 (플랫폼 계층 mealkit/platform/Gamepad.h가 채운다). 스틱 y는 위가 +.
namespace pad {
inline constexpr u16 A = 1u << 0, B = 1u << 1, X = 1u << 2, Y = 1u << 3;
inline constexpr u16 LeftShoulder = 1u << 4, RightShoulder = 1u << 5, Back = 1u << 6, Start = 1u << 7;
inline constexpr u16 LeftThumb = 1u << 8, RightThumb = 1u << 9;
inline constexpr u16 DPadUp = 1u << 10, DPadDown = 1u << 11, DPadLeft = 1u << 12, DPadRight = 1u << 13;
} // namespace pad

struct PadState {
    bool connected = false;
    u16 buttons = 0; // pad::A ...
    f32 leftX = 0.0f, leftY = 0.0f, rightX = 0.0f, rightY = 0.0f; // -1..1 (데드존 전)
    f32 leftTrigger = 0.0f, rightTrigger = 0.0f;                 // 0..1
};

// 이름 ↔ 코드 (바인딩 파일과 설정 화면이 쓰는 이름). 모르는 이름은 0.
u8 keyFromName(std::string_view name);
const char* keyName(u8 vk); // 이름이 없는 키는 nullptr

// 마지막으로 쓴 장치 (버튼 표시를 장치에 맞출 때)
enum class Device : u8 { KeyboardMouse, Gamepad };

// 엔진이 프로세스(창)마다 하나 만들어 GameContext::actions로 넘긴다. 게임은 읽기만 한다.
class Actions {
public:
    Actions();
    ~Actions();
    Actions(const Actions&) = delete;
    Actions& operator=(const Actions&) = delete;

    // 플레이어가 바꾼 바인딩 파일 (보통 game::userFolder(설정) + "/input.bindings"). 빈 문자열이면 없음.
    void setUserFile(std::string path);
    // assets/input.bindings와 사용자 파일을 읽는다. update가 0.25초마다 파일 시각을 보고 다시 읽는다.
    void load();

    // 매 프레임 한 번 (엔진, 게임 update 전). 허용되지 않은 장치는 아무것도 누르지 않은 것으로 본다.
    void update(const Input& input, const PadState& pad, bool keyboardAllowed, bool mouseAllowed, f64 dt);
    // 모든 값을 놓은 상태로 (창이 포커스를 잃었을 때, 매치를 다시 시작할 때)
    void clear();

    // 이름으로 읽는다. 없는 액션은 0이고 이름마다 한 번 경고한다.
    bool down(std::string_view action) const;
    bool pressed(std::string_view action) const;  // 이번 프레임에 눌림
    bool released(std::string_view action) const; // 이번 프레임에 뗌
    f32 axis(std::string_view action) const;      // Axis, Delta (Button은 0/1)
    Vec2 axis2(std::string_view action) const;    // Axis2, Delta2
    Device lastDevice() const;

    // 바인딩 (설정 화면): 지금 값(프로젝트 + 사용자), 바꾸기, 되돌리기, 사용자 파일에 저장(프로젝트와 다른 행만)
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

// 에디터 [표] 패널에 나오는 엔진 표 (바인딩)
const reflect::TableList& engineTables();

} // namespace mealkit::input
