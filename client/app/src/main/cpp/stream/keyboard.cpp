#include "stream/keyboard.h"

#include <android/keycodes.h>

#include <Limelight.h>

#include "stream/stream_state.h"

namespace {

// Android keycode -> Windows virtual-key code, which is what the GameStream
// protocol carries (Sunshine maps it back to Linux keycodes). Follows
// moonlight-android's KeyboardTranslator, with real Windows VK values.
short toVirtualKey(int32_t k) {
  if (k >= AKEYCODE_0 && k <= AKEYCODE_9) return static_cast<short>(0x30 + (k - AKEYCODE_0));
  if (k >= AKEYCODE_A && k <= AKEYCODE_Z) return static_cast<short>(0x41 + (k - AKEYCODE_A));
  if (k >= AKEYCODE_NUMPAD_0 && k <= AKEYCODE_NUMPAD_9) return static_cast<short>(0x60 + (k - AKEYCODE_NUMPAD_0));
  if (k >= AKEYCODE_F1 && k <= AKEYCODE_F12) return static_cast<short>(0x70 + (k - AKEYCODE_F1));
  switch (k) {
    case AKEYCODE_DEL: return 0x08;            // VK_BACK
    case AKEYCODE_TAB: return 0x09;
    case AKEYCODE_CLEAR: return 0x0C;
    case AKEYCODE_ENTER: return 0x0D;
    case AKEYCODE_NUMPAD_ENTER: return 0x0D;
    case AKEYCODE_BREAK: return 0x13;          // VK_PAUSE
    case AKEYCODE_CAPS_LOCK: return 0x14;
    case AKEYCODE_ESCAPE: return 0x1B;
    case AKEYCODE_SPACE: return 0x20;
    case AKEYCODE_PAGE_UP: return 0x21;
    case AKEYCODE_PAGE_DOWN: return 0x22;
    case AKEYCODE_MOVE_END: return 0x23;
    case AKEYCODE_MOVE_HOME: return 0x24;
    case AKEYCODE_DPAD_LEFT: return 0x25;
    case AKEYCODE_DPAD_UP: return 0x26;
    case AKEYCODE_DPAD_RIGHT: return 0x27;
    case AKEYCODE_DPAD_DOWN: return 0x28;
    case AKEYCODE_SYSRQ: return 0x2C;          // VK_SNAPSHOT (Print Screen)
    case AKEYCODE_INSERT: return 0x2D;
    case AKEYCODE_FORWARD_DEL: return 0x2E;
    case AKEYCODE_META_LEFT: return 0x5B;      // VK_LWIN -> Super, Hyprland's mod key
    case AKEYCODE_META_RIGHT: return 0x5C;
    case AKEYCODE_MENU: return 0x5D;           // VK_APPS
    case AKEYCODE_NUMPAD_MULTIPLY: return 0x6A;
    case AKEYCODE_NUMPAD_ADD: return 0x6B;
    case AKEYCODE_NUMPAD_SUBTRACT: return 0x6D;
    case AKEYCODE_NUMPAD_DOT: return 0x6E;
    case AKEYCODE_NUMPAD_DIVIDE: return 0x6F;
    case AKEYCODE_NUM_LOCK: return 0x90;
    case AKEYCODE_SCROLL_LOCK: return 0x91;
    case AKEYCODE_SHIFT_LEFT: return 0xA0;
    case AKEYCODE_SHIFT_RIGHT: return 0xA1;
    case AKEYCODE_CTRL_LEFT: return 0xA2;
    case AKEYCODE_CTRL_RIGHT: return 0xA3;
    case AKEYCODE_ALT_LEFT: return 0xA4;
    case AKEYCODE_ALT_RIGHT: return 0xA5;
    case AKEYCODE_SEMICOLON: return 0xBA;
    case AKEYCODE_PLUS:
    case AKEYCODE_EQUALS: return 0xBB;
    case AKEYCODE_COMMA: return 0xBC;
    case AKEYCODE_MINUS: return 0xBD;
    case AKEYCODE_PERIOD: return 0xBE;
    case AKEYCODE_SLASH: return 0xBF;
    case AKEYCODE_GRAVE: return 0xC0;
    case AKEYCODE_LEFT_BRACKET: return 0xDB;
    case AKEYCODE_BACKSLASH: return 0xDC;
    case AKEYCODE_RIGHT_BRACKET: return 0xDD;
    case AKEYCODE_APOSTROPHE: return 0xDE;
    default: return 0;
  }
}

char toModifiers(int32_t meta) {
  char m = 0;
  if (meta & AMETA_SHIFT_ON) m |= MODIFIER_SHIFT;
  if (meta & AMETA_CTRL_ON) m |= MODIFIER_CTRL;
  if (meta & AMETA_ALT_ON) m |= MODIFIER_ALT;
  if (meta & AMETA_META_ON) m |= MODIFIER_META;
  return m;
}

}  // namespace

bool forwardKey(int32_t keyCode, int32_t action, int32_t metaState, int32_t repeatCount) {
  if (!streamConnected()) return false;
  const short vk = toVirtualKey(keyCode);
  if (vk == 0) return false;

  if (action == AKEY_EVENT_ACTION_DOWN) {
    // The host repeats held keys itself; Android's repeats would double them.
    if (repeatCount > 0) return true;
    LiSendKeyboardEvent(static_cast<short>(0x8000 | vk), KEY_ACTION_DOWN, toModifiers(metaState));
    return true;
  }
  if (action == AKEY_EVENT_ACTION_UP) {
    LiSendKeyboardEvent(static_cast<short>(0x8000 | vk), KEY_ACTION_UP, toModifiers(metaState));
    return true;
  }
  return false;
}

bool forwardKeyEvent(const AInputEvent* event) {
  return forwardKey(AKeyEvent_getKeyCode(event), AKeyEvent_getAction(event), AKeyEvent_getMetaState(event),
                    AKeyEvent_getRepeatCount(event));
}
