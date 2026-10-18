#include "mealkit/input/Actions.h"

#include "mealkit/core/Log.h"
#include "mealkit/game/Tables.h"
#include "mealkit/platform/Input.h"
#include "mealkit/platform/Paths.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace mealkit::input {
namespace {

constexpr f64 kFileCheckSeconds = 0.25;
constexpr f32 kTriggerPressed = 0.5f; // 트리거를 버튼으로 쓸 때 눌림으로 보는 값

struct KeyName {
    const char* name;
    u8 vk;
};
// 글자(A-Z)와 숫자(0-9)는 그 문자 그대로 (Win32 가상 키 코드와 같다)
constexpr KeyName kKeyNames[] = {
    {"Backspace", key::Backspace}, {"Tab", key::Tab},     {"Enter", key::Enter}, {"Shift", key::Shift},
    {"Control", key::Control},     {"Alt", key::Alt},     {"Escape", key::Escape}, {"Space", key::Space},
    {"Left", key::Left},           {"Up", key::Up},       {"Right", key::Right}, {"Down", key::Down},
    {"F1", 0x70},  {"F2", 0x71},  {"F3", 0x72},  {"F4", 0x73},  {"F5", 0x74},  {"F6", 0x75},
    {"F7", 0x76},  {"F8", 0x77},  {"F9", 0x78},  {"F10", 0x79}, {"F11", 0x7A}, {"F12", 0x7B},
};

enum class MouseSource : u8 { Left, Right, Middle, WheelUp, WheelDown, Wheel, Move, MoveX, MoveY };
constexpr const char* kMouseNames[] = {"Left", "Right", "Middle", "WheelUp", "WheelDown", "Wheel", "Move", "MoveX", "MoveY"};

enum class PadAnalog : u8 { None, LeftX, LeftY, RightX, RightY, LeftTrigger, RightTrigger, LeftStick, RightStick, DPad };
struct PadName {
    const char* name;
    u16 button;
    PadAnalog analog;
};
constexpr PadName kPadNames[] = {
    {"A", pad::A, PadAnalog::None},
    {"B", pad::B, PadAnalog::None},
    {"X", pad::X, PadAnalog::None},
    {"Y", pad::Y, PadAnalog::None},
    {"LeftShoulder", pad::LeftShoulder, PadAnalog::None},
    {"RightShoulder", pad::RightShoulder, PadAnalog::None},
    {"Back", pad::Back, PadAnalog::None},
    {"Start", pad::Start, PadAnalog::None},
    {"LeftThumb", pad::LeftThumb, PadAnalog::None},
    {"RightThumb", pad::RightThumb, PadAnalog::None},
    {"DPadUp", pad::DPadUp, PadAnalog::None},
    {"DPadDown", pad::DPadDown, PadAnalog::None},
    {"DPadLeft", pad::DPadLeft, PadAnalog::None},
    {"DPadRight", pad::DPadRight, PadAnalog::None},
    {"LeftX", 0, PadAnalog::LeftX},
    {"LeftY", 0, PadAnalog::LeftY},
    {"RightX", 0, PadAnalog::RightX},
    {"RightY", 0, PadAnalog::RightY},
    {"LeftTrigger", 0, PadAnalog::LeftTrigger},
    {"RightTrigger", 0, PadAnalog::RightTrigger},
    {"LeftStick", 0, PadAnalog::LeftStick},
    {"RightStick", 0, PadAnalog::RightStick},
    {"DPad", 0, PadAnalog::DPad},
};

std::vector<std::string> words(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
        size_t j = i;
        while (j < s.size() && s[j] != ' ' && s[j] != '\t') ++j;
        if (j > i) out.push_back(s.substr(i, j - i));
        i = j;
    }
    return out;
}

bool is2D(ActionKind k) { return k == ActionKind::Axis2 || k == ActionKind::Delta2; }
bool isDelta(ActionKind k) { return k == ActionKind::Delta || k == ActionKind::Delta2; }

// 데드존 밖만 0..1로 다시 편다
f32 dead1(f32 v, f32 dz) {
    const f32 a = std::fabs(v);
    if (a <= dz || dz >= 1.0f) return 0.0f;
    return std::copysign(std::min((a - dz) / (1.0f - dz), 1.0f), v);
}
Vec2 dead2(Vec2 v, f32 dz) {
    const f32 len = length(v);
    if (len <= dz || dz >= 1.0f) return {};
    return v * (std::min((len - dz) / (1.0f - dz), 1.0f) / len);
}

// 한 행을 읽어 둔 것: 이름을 매 프레임 찾지 않는다
struct Bound {
    std::string id;
    Action def;
    std::vector<u8> keys; // 0 = 빈 자리("-")
    std::vector<MouseSource> mouse;
    std::vector<const PadName*> pad;
    bool down = false, prevDown = false;
    f32 value = 0.0f;
    Vec2 value2{};
};

void parse(Bound& b, std::string* warnings) {
    b.keys.clear();
    b.mouse.clear();
    b.pad.clear();
    auto warn = [&](const char* what, const std::string& name) {
        if (warnings) *warnings += "action " + b.id + ": unknown " + what + " '" + name + "'\n";
    };
    for (const std::string& w : words(b.def.keys)) {
        const u8 vk = w == "-" ? 0 : keyFromName(w);
        if (!vk && w != "-") warn("key", w);
        b.keys.push_back(vk);
    }
    const size_t group = b.def.kind == ActionKind::Axis || b.def.kind == ActionKind::Delta ? 2 : is2D(b.def.kind) ? 4 : 1;
    if (b.keys.size() % group != 0 && warnings)
        *warnings += "action " + b.id + ": keys come in groups of " + std::to_string(group) + "\n";
    for (const std::string& w : words(b.def.mouse)) {
        bool found = false;
        for (u32 i = 0; i < std::size(kMouseNames); ++i)
            if (w == kMouseNames[i]) {
                b.mouse.push_back(static_cast<MouseSource>(i));
                found = true;
            }
        if (!found) warn("mouse input", w);
    }
    for (const std::string& w : words(b.def.pad)) {
        const PadName* p = nullptr;
        for (const PadName& n : kPadNames)
            if (w == n.name) p = &n;
        if (p) b.pad.push_back(p);
        else warn("gamepad input", w);
    }
}

bool sameBinding(const Action& a, const Action& b) {
    return a.keys == b.keys && a.mouse == b.mouse && a.pad == b.pad && a.rate == b.rate && a.deadzone == b.deadzone;
}

// 사용자 파일에 남기는 필드 (종류·이름은 프로젝트 것을 쓴다)
bool userField(const std::string& path) {
    return path == "keys" || path == "mouse" || path == "pad" || path == "rate" || path == "deadzone";
}

} // namespace

u8 keyFromName(std::string_view name) {
    if (name.size() == 1) {
        const char c = name[0];
        if (c >= 'A' && c <= 'Z') return static_cast<u8>(c);
        if (c >= 'a' && c <= 'z') return static_cast<u8>(c - 'a' + 'A');
        if (c >= '0' && c <= '9') return static_cast<u8>(c);
    }
    for (const KeyName& k : kKeyNames)
        if (name == k.name) return k.vk;
    return 0;
}

const char* keyName(u8 vk) {
    static char letters[36][2];
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
        const u32 i = vk >= 'A' ? 10u + (vk - 'A') : static_cast<u32>(vk - '0');
        letters[i][0] = static_cast<char>(vk);
        letters[i][1] = 0;
        return letters[i];
    }
    for (const KeyName& k : kKeyNames)
        if (vk == k.vk) return k.name;
    return nullptr;
}

struct Actions::Impl {
    std::vector<Bound> actions;
    std::vector<Action> project; // 프로젝트 파일 값 (되돌리기와 사용자 파일의 차이 비교)
    std::string userFile;
    std::string projectPath;
    unsigned long long projectTime = 0, userTime = 0;
    f64 time = 0.0, nextCheck = 0.0;
    Device device = Device::KeyboardMouse;
    mutable std::vector<std::string> warned;

    const Bound* find(std::string_view name) const {
        for (const Bound& b : actions)
            if (b.id == name) return &b;
        for (const std::string& w : warned)
            if (w == name) return nullptr;
        warned.emplace_back(name);
        MEALKIT_WARN("Input: no action '%.*s' in assets/%s", static_cast<int>(name.size()), name.data(), kBindingsTable);
        return nullptr;
    }
    Bound* find(std::string_view name) { return const_cast<Bound*>(static_cast<const Impl*>(this)->find(name)); }

    void load() {
        projectPath = findAsset(std::string("assets/") + kBindingsTable);
        projectTime = projectPath.empty() ? 0 : fileWriteTime(projectPath);
        userTime = userFile.empty() ? 0 : fileWriteTime(userFile);
        sim::Table<Action> table;
        if (!projectPath.empty()) game::loadTable(table, kBindingsTable);
        else MEALKIT_WARN("Input: assets/%s not found - the game has no actions", kBindingsTable);
        project = table.rows;
        std::vector<Bound> next(table.size());
        for (u32 i = 0; i < table.size(); ++i) {
            next[i].id = table.ids[i];
            next[i].def = table.rows[i];
        }
        // 사용자 파일: 같은 id 행의 바인딩 필드만 덮는다 (없어진 액션은 무시)
        if (userTime != 0) {
            game::TableText text;
            if (game::readTableText(userFile, text)) {
                for (size_t r = 0; r < text.ids.size(); ++r)
                    for (Bound& b : next)
                        if (b.id == text.ids[r]) {
                            reflect::TextFields fields;
                            for (const auto& f : text.rows[r])
                                if (userField(f.first)) fields.push_back(f);
                            reflect::fromText(reflect::typeOf<Action>(), &b.def, fields);
                        }
            }
        }
        std::string warnings;
        for (Bound& b : next) {
            parse(b, &warnings);
            for (const Bound& old : actions) // 다시 읽어도 누르고 있던 상태는 이어 간다
                if (old.id == b.id) b.down = b.prevDown = old.down;
        }
        if (!warnings.empty()) MEALKIT_WARN("Input: assets/%s:\n%s", kBindingsTable, warnings.c_str());
        actions = std::move(next);
        warned.clear();
    }

    void poll() {
        if (time < nextCheck) return;
        nextCheck = time + kFileCheckSeconds;
        const std::string path = findAsset(std::string("assets/") + kBindingsTable);
        const unsigned long long t = path.empty() ? 0 : fileWriteTime(path);
        const unsigned long long u = userFile.empty() ? 0 : fileWriteTime(userFile);
        if (path != projectPath || t != projectTime || u != userTime) {
            load();
            MEALKIT_INFO("Input: %u action(s) from %s", static_cast<u32>(actions.size()),
                         path.empty() ? "(no input.bindings)" : path.c_str());
        }
    }
};

Actions::Actions() : m(new Impl()) {}
Actions::~Actions() { delete m; }

void Actions::setUserFile(std::string path) { m->userFile = std::move(path); }
void Actions::load() { m->load(); }

void Actions::update(const Input& input, const PadState& padState, bool keyboardAllowed, bool mouseAllowed, f64 dt) {
    m->time += dt;
    m->poll();
    const f32 seconds = static_cast<f32>(dt);
    bool usedKeyboard = false, usedPad = false;
    for (Bound& b : m->actions) {
        const ActionKind kind = b.def.kind;
        const f32 dz = std::clamp(b.def.deadzone, 0.0f, 0.99f);
        const f32 rate = isDelta(kind) ? b.def.rate * seconds : 1.0f; // 키·스틱 → 이번 프레임 변화량
        bool down = false;
        f32 v = 0.0f, mouseV = 0.0f;
        Vec2 v2{}, mouse2{};
        auto key = [&](size_t i) { return keyboardAllowed && b.keys[i] && input.keyDown(b.keys[i]); };

        // 키보드
        if (kind == ActionKind::Button) {
            for (size_t i = 0; i < b.keys.size(); ++i) down = down || key(i);
        } else if (is2D(kind)) {
            for (size_t i = 0; i + 3 < b.keys.size(); i += 4)
                v2 += Vec2{(key(i + 3) ? 1.0f : 0.0f) - (key(i + 2) ? 1.0f : 0.0f), (key(i) ? 1.0f : 0.0f) - (key(i + 1) ? 1.0f : 0.0f)};
        } else {
            for (size_t i = 0; i + 1 < b.keys.size(); i += 2) v += (key(i + 1) ? 1.0f : 0.0f) - (key(i) ? 1.0f : 0.0f);
        }
        usedKeyboard = usedKeyboard || down || v != 0.0f || v2.x != 0.0f || v2.y != 0.0f;

        // 마우스 (픽셀·휠 칸 그대로, rate를 곱하지 않는다). 위가 +
        if (mouseAllowed)
            for (MouseSource s : b.mouse) {
                switch (s) {
                case MouseSource::Left: down = down || input.mouseDown(MouseButton::Left); break;
                case MouseSource::Right: down = down || input.mouseDown(MouseButton::Right); break;
                case MouseSource::Middle: down = down || input.mouseDown(MouseButton::Middle); break;
                case MouseSource::WheelUp: down = down || input.wheelDelta() > 0.0f; break;
                case MouseSource::WheelDown: down = down || input.wheelDelta() < 0.0f; break;
                case MouseSource::Wheel: mouseV += input.wheelDelta(); break;
                case MouseSource::Move: mouse2 += Vec2{input.mouseDeltaX(), -input.mouseDeltaY()}; break;
                case MouseSource::MoveX: mouseV += input.mouseDeltaX(); break;
                case MouseSource::MoveY: mouseV -= input.mouseDeltaY(); break;
                }
                usedKeyboard = usedKeyboard || down || mouseV != 0.0f || mouse2.x != 0.0f || mouse2.y != 0.0f;
            }

        // 게임패드
        if (padState.connected)
            for (const PadName* p : b.pad) {
                f32 a = 0.0f;
                Vec2 a2{};
                const bool buttonDown = p->button && (padState.buttons & p->button);
                switch (p->analog) {
                case PadAnalog::None: a = buttonDown ? 1.0f : 0.0f; break;
                case PadAnalog::LeftX: a = dead1(padState.leftX, dz); break;
                case PadAnalog::LeftY: a = dead1(padState.leftY, dz); break;
                case PadAnalog::RightX: a = dead1(padState.rightX, dz); break;
                case PadAnalog::RightY: a = dead1(padState.rightY, dz); break;
                case PadAnalog::LeftTrigger: a = dead1(padState.leftTrigger, dz); break;
                case PadAnalog::RightTrigger: a = dead1(padState.rightTrigger, dz); break;
                case PadAnalog::LeftStick: a2 = dead2({padState.leftX, padState.leftY}, dz); break;
                case PadAnalog::RightStick: a2 = dead2({padState.rightX, padState.rightY}, dz); break;
                case PadAnalog::DPad: {
                    const u16 bt = padState.buttons;
                    a2 = {((bt & pad::DPadRight) ? 1.0f : 0.0f) - ((bt & pad::DPadLeft) ? 1.0f : 0.0f),
                          ((bt & pad::DPadUp) ? 1.0f : 0.0f) - ((bt & pad::DPadDown) ? 1.0f : 0.0f)};
                    break;
                }
                }
                if (kind == ActionKind::Button) down = down || buttonDown || (p->analog != PadAnalog::None && a >= kTriggerPressed);
                else if (is2D(kind)) v2 += a2;
                else v += a;
                usedPad = usedPad || buttonDown || a != 0.0f || a2.x != 0.0f || a2.y != 0.0f;
            }

        b.prevDown = b.down;
        if (kind == ActionKind::Button) {
            b.down = down;
            b.value = down ? 1.0f : 0.0f;
            b.value2 = {};
        } else if (is2D(kind)) {
            v2 = v2 * rate;
            if (kind == ActionKind::Axis2) {
                v2 += mouse2;
                const f32 len = length(v2);
                if (len > 1.0f) v2 = v2 * (1.0f / len); // 대각선 키·스틱 + 키는 길이 1로
            } else {
                v2 += mouse2;
            }
            b.value2 = v2;
            b.value = 0.0f;
            b.down = v2.x != 0.0f || v2.y != 0.0f;
        } else {
            v = v * rate + mouseV;
            if (kind == ActionKind::Axis) v = std::clamp(v, -1.0f, 1.0f);
            b.value = v;
            b.value2 = {};
            b.down = v != 0.0f;
        }
    }
    if (usedPad) m->device = Device::Gamepad;
    else if (usedKeyboard) m->device = Device::KeyboardMouse;
}

void Actions::clear() {
    for (Bound& b : m->actions) {
        b.down = b.prevDown = false;
        b.value = 0.0f;
        b.value2 = {};
    }
}

bool Actions::down(std::string_view action) const {
    const Bound* b = m->find(action);
    return b && b->down;
}
bool Actions::pressed(std::string_view action) const {
    const Bound* b = m->find(action);
    return b && b->down && !b->prevDown;
}
bool Actions::released(std::string_view action) const {
    const Bound* b = m->find(action);
    return b && !b->down && b->prevDown;
}
f32 Actions::axis(std::string_view action) const {
    const Bound* b = m->find(action);
    return b ? b->value : 0.0f;
}
Vec2 Actions::axis2(std::string_view action) const {
    const Bound* b = m->find(action);
    return b ? b->value2 : Vec2{};
}
Device Actions::lastDevice() const { return m->device; }

u32 Actions::count() const { return static_cast<u32>(m->actions.size()); }
const std::string& Actions::id(u32 index) const { return m->actions[index].id; }
const Action& Actions::binding(u32 index) const { return m->actions[index].def; }

bool Actions::setBinding(std::string_view action, const Action& binding) {
    Bound* b = m->find(action);
    if (!b) return false;
    const ActionKind kind = b->def.kind;
    const std::string label = b->def.label;
    b->def = binding;
    b->def.kind = kind; // 종류·이름은 게임이 정한다
    b->def.label = label;
    std::string warnings;
    parse(*b, &warnings);
    if (!warnings.empty()) MEALKIT_WARN("Input: %s", warnings.c_str());
    return warnings.empty();
}

void Actions::resetBinding(std::string_view action) {
    Bound* b = m->find(action);
    if (!b) return;
    b->def = m->project[static_cast<size_t>(b - m->actions.data())];
    parse(*b, nullptr);
}

bool Actions::saveUser() const {
    if (m->userFile.empty()) return false;
    game::TableText text;
    for (size_t i = 0; i < m->actions.size(); ++i) {
        const Bound& b = m->actions[i];
        if (i < m->project.size() && sameBinding(b.def, m->project[i])) continue;
        reflect::TextFields all, fields;
        reflect::toText(reflect::typeOf<Action>(), &b.def, all);
        for (const auto& f : all)
            if (userField(f.first)) fields.push_back(f);
        text.ids.push_back(b.id);
        text.rows.push_back(std::move(fields));
    }
    if (text.ids.empty()) { // 모두 기본값: 파일을 지운다
        if (fileExists(m->userFile)) removeFile(m->userFile);
        m->userTime = 0;
        return true;
    }
    createDirectories(parentDirectory(m->userFile));
    if (!game::writeTableText(m->userFile, text, "Action")) return false;
    m->userTime = fileWriteTime(m->userFile);
    return true;
}

const reflect::TableList& engineTables() {
    static const reflect::TableList list{{&reflect::typeOf<Action>(), kBindingsTable, reflect::kTableView}};
    return list;
}

} // namespace mealkit::input
