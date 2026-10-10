#pragma once

// Kusanagi's lock screen look on top of LockSurface: clock and date, avatar and greeting, the
// password pill, the error line, a now-playing pill and the test-mode banner, laid out by lock.style. The
// engine still owns the session lock, PAM and the hidden password field; this only draws.

#include "core/timer_manager.h"
#include "render/animation/animation_manager.h"
#include "render/core/renderer.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class Box;
class Image;
class InputArea;
class Label;
class MprisService;
class Node;
class Renderer;

namespace kusanagi {

  // True when Kusanagi settings are loaded: lock surfaces then use this look instead of the default login box.
  [[nodiscard]] bool lockLookEnabled();

  class LockView {
  public:
    struct Prompt {
      std::string who;             // lock.greeting, or the user name
      std::size_t passwordLength = 0;
      bool busy = false;           // PAM is checking
      std::string error;           // e.g. "Wrong password"; empty for none
      std::uint64_t failures = 0;  // bumps on every failed attempt, which shakes the pill
      bool testMode = false;
      bool focused = true;         // the password field has the keyboard
    };

    // Builds its nodes under `parent` (z above the wallpaper); the callbacks reach the surface.
    LockView(
        Node& parent, AnimationManager& animations, std::function<void()> requestUpdate,
        std::function<void()> requestLayout, std::function<void()> requestRedraw
    );
    ~LockView();

    LockView(const LockView&) = delete;
    LockView& operator=(const LockView&) = delete;

    void setMpris(MprisService* mpris) noexcept { m_mpris = mpris; }
    // The login screen (kusanagi-shell --greeter) shows the picked user's picture instead of ~/.face.
    void setAvatarPath(std::string path);
    void setPrompt(const Prompt& prompt);
    void setHidden(bool hidden);

    // Update phase: clock / date / media text, avatar.
    void sync(Renderer& renderer);
    // Layout phase: place everything for a width x height output.
    void layout(Renderer& renderer, float width, float height);

    // Fade and zoom in on lock, dissolve outward on unlock.
    void playEnter();
    void playExit(std::function<void()> done);

    [[nodiscard]] float dim() const; // lock.dim, for the wallpaper overlay

  private:
    struct MediaButton {
      Box* bg = nullptr;
      Label* icon = nullptr;
      InputArea* area = nullptr;
      bool filled = false;
      bool hovered = false;
    };

    void build();
    void readStyle();
    void syncDots();
    void syncIcon();
    void startSpin();
    void startShake();
    void applyButtonLook(MediaButton& button);
    void layoutClock(Renderer& renderer, float width, float height);
    void layoutLogin(Renderer& renderer, float width, float height);
    void layoutTerminal(Renderer& renderer, float width, float height);
    void layoutMedia(Renderer& renderer, float width, float height);
    void layoutBanner(Renderer& renderer, float width);
    void setContentProgress(float opacity, float scale);
    [[nodiscard]] float lineHeight(Renderer& renderer, float size, FontWeight weight) const;

    Node& m_parent;
    AnimationManager& m_animations;
    std::function<void()> m_requestUpdate;
    std::function<void()> m_requestLayout;
    std::function<void()> m_requestRedraw;
    MprisService* m_mpris = nullptr;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);

    // Settings, re-read on every sync so config reloads apply live.
    std::string m_style = "center";
    std::string m_clockFormat = "HH:mm";
    std::string m_font;
    bool m_avatarOn = true;
    bool m_mediaOn = true;

    Prompt m_prompt;
    bool m_hidden = false;
    float m_width = 0.0F;
    float m_height = 0.0F;

    Node* m_content = nullptr;
    // split / card backgrounds
    Box* m_splitPanel = nullptr;
    Box* m_splitLine = nullptr;
    Box* m_card = nullptr;
    // clock column
    Label* m_time = nullptr;
    Label* m_minutes = nullptr;
    Label* m_date = nullptr;
    // terminal
    Node* m_term = nullptr;
    Label* m_termHeader = nullptr;
    Label* m_termLogin = nullptr;
    Label* m_termPassword = nullptr;
    Box* m_termCursor = nullptr;
    Label* m_termBusy = nullptr;
    Label* m_termError = nullptr;
    // you + password
    Node* m_login = nullptr;
    Box* m_avatarBg = nullptr;
    Image* m_avatar = nullptr;
    Box* m_avatarRing = nullptr;
    Label* m_name = nullptr;
    Node* m_pill = nullptr;
    Box* m_pillBg = nullptr;
    Label* m_pillIcon = nullptr;
    Node* m_dotRow = nullptr;
    std::vector<Box*> m_dots;
    Label* m_placeholder = nullptr;
    Label* m_error = nullptr;
    // now playing
    Node* m_media = nullptr;
    Box* m_mediaBg = nullptr;
    Label* m_mediaIcon = nullptr;
    Label* m_mediaText = nullptr;
    MediaButton m_prev;
    MediaButton m_play;
    MediaButton m_next;
    bool m_mediaVisible = false;
    // test banner
    Node* m_banner = nullptr;
    Box* m_bannerBg = nullptr;
    Label* m_bannerText = nullptr;

    std::string m_host;
    std::string m_kernel;
    std::string m_avatarPath;
    bool m_avatarLoaded = false;

    float m_clockBottom = 0.0F;
    float m_pillBaseX = 0.0F;
    float m_shake = 0.0F;
    float m_spin = 0.0F;
    bool m_cursorOn = true;
    std::uint64_t m_seenFailures = 0;
    AnimationManager::Id m_shakeAnim = 0;
    AnimationManager::Id m_spinAnim = 0;
    AnimationManager::Id m_contentAnim = 0;
    Timer m_clockTimer;
    Timer m_cursorTimer;
  };

} // namespace kusanagi
