// Front-end-only debug shortcuts. Kept independent of SDL so the original
// Amiga F9/F10 throttle keys can be regression-tested without the game/ADF.
#pragma once

enum class FrontendKey { Other, F9, F10 };
enum class FrontendAction { PassToGame, Consumed, OpenMenu, OpenModels };

class FrontendHotkeys {
public:
  FrontendAction key(FrontendKey key, bool down, bool ctrl, bool repeat) {
    if (key == FrontendKey::Other) return FrontendAction::PassToGame;
    bool& swallowed = key == FrontendKey::F9 ? f9_swallowed_ : f10_swallowed_;
    if (down && ctrl) {
      swallowed = true;
      if (repeat) return FrontendAction::Consumed;
      return key == FrontendKey::F9 ? FrontendAction::OpenMenu
                                    : FrontendAction::OpenModels;
    }
    // A modifier may be released before the function key. Never send an
    // orphan key-up to the game when its key-down was a host shortcut.
    if (!down && swallowed) {
      swallowed = false;
      return FrontendAction::Consumed;
    }
    // Also recover if a modal debug menu ate the shortcut's key-up.
    if (down) swallowed = false;
    return FrontendAction::PassToGame;
  }

private:
  bool f9_swallowed_ = false;
  bool f10_swallowed_ = false;
};
