#include "terminal.hpp"
#include "imgui.h"
#include "imgui_internal.h" // ImTextCharToUtf8, GImGui->PlatformImeData
#include <ghostty/vt.h>
#include <pty.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>
#include <tracy/Tracy.hpp>

using namespace KR;

namespace {
constexpr float fontSize = 16.0f;
constexpr float pad = 4.0f; // margin between the window content edge and the grid
constexpr float barWidth = 6.0f;
constexpr float barMargin = 2.0f;
} // namespace

struct TerminalSession {
    GhosttyTerminal terminal = nullptr;
    GhosttyKeyEncoder keyEncoder = nullptr;
    GhosttyKeyEvent keyEvent = nullptr;
    GhosttyMouseEncoder mouseEncoder = nullptr;
    GhosttyMouseEvent mouseEvent = nullptr;
    GhosttyRenderState renderState = nullptr;
    GhosttyRenderStateRowIterator rowIter = nullptr;
    GhosttyRenderStateRowCells rowCells = nullptr;
    int ptyFd = -1;
    pid_t child = -1;
    uint16_t cols = 80;
    uint16_t rows = 24;
    int cellWidth = 1;
    int cellHeight = 1;
    std::string title = "Terminal";
    bool focused = false;
    bool scrollbarDragging = false;
    bool childExited = false; // the pty hit EOF/error
    bool childReaped = false;
    int childExitStatus = -1;

    ~TerminalSession() {
        if (ptyFd >= 0) {
            close(ptyFd);
        }
        if (child > 0 && !childReaped) {
            if (!childExited) {
                kill(child, SIGHUP);
            }
            waitpid(child, nullptr, 0);
        }
        if (mouseEvent) { ghostty_mouse_event_free(mouseEvent); }
        if (mouseEncoder) { ghostty_mouse_encoder_free(mouseEncoder); }
        if (keyEvent) { ghostty_key_event_free(keyEvent); }
        if (keyEncoder) { ghostty_key_encoder_free(keyEncoder); }
        if (rowCells) { ghostty_render_state_row_cells_free(rowCells); }
        if (rowIter) { ghostty_render_state_row_iterator_free(rowIter); }
        if (renderState) { ghostty_render_state_free(renderState); }
        if (terminal) { ghostty_terminal_free(terminal); }
    }
};

namespace {

// ---------------------------------------------------------------------------
// PTY helpers
// ---------------------------------------------------------------------------

winsize windowSize(const TerminalSession &s) {
    ZoneScoped;
    winsize ws{};
    ws.ws_row = s.rows;
    ws.ws_col = s.cols;
    ws.ws_xpixel = (unsigned short)(s.cols * s.cellWidth);
    ws.ws_ypixel = (unsigned short)(s.rows * s.cellHeight);
    return ws;
}

bool ptySpawn(TerminalSession &s) {
    ZoneScoped;
    winsize ws = windowSize(s);
    pid_t child = forkpty(&s.ptyFd, nullptr, nullptr, &ws);
    if (child < 0) {
        perror("forkpty");
        return false;
    }
    if (child == 0) {
        const char *shell = getenv("SHELL");
        if (!shell || shell[0] == '\0') {
            struct passwd *pw = getpwuid(getuid());
            if (pw && pw->pw_shell && pw->pw_shell[0] != '\0') {
                shell = pw->pw_shell;
            } else {
                shell = "/bin/sh";
            }
        }
        const char *shellName = shell;
        if (const char *slash = strrchr(shell, '/')) {
            shellName = slash + 1;
        }
        setenv("TERM", "xterm-256color", 1);
        execl(shell, shellName, (char *)nullptr);
        _exit(127);
    }
    s.child = child;
    int flags = fcntl(s.ptyFd, F_GETFL);
    if (flags < 0 || fcntl(s.ptyFd, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("fcntl O_NONBLOCK");
        return false;
    }
    return true;
}

// Best effort: retries EINTR, drops the rest on EAGAIN like most terminals.
void ptyWrite(int fd, const char *buf, size_t len) {
    ZoneScoped;
    while (len > 0) {
        ssize_t n = write(fd, buf, len);
        if (n > 0) {
            buf += n;
            len -= (size_t)n;
        } else if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
    }
}

// Feeds everything the shell wrote into the VT parser. False once the child
// side is closed (Linux reports that as EIO more often than a clean EOF).
bool ptyRead(TerminalSession &s) {
    ZoneScoped;
    uint8_t buf[4096];
    for (;;) {
        ssize_t n = read(s.ptyFd, buf, sizeof(buf));
        if (n > 0) {
            ghostty_terminal_vt_write(s.terminal, buf, (size_t)n);
        } else if (n == 0) {
            return false;
        } else if (errno == EAGAIN) {
            return true;
        } else if (errno != EINTR) {
            if (errno != EIO) {
                perror("pty read");
            }
            return false;
        }
    }
}

// ---------------------------------------------------------------------------
// Effects: the terminal's replies to queries from the running program
// ---------------------------------------------------------------------------

void effectWritePty(GhosttyTerminal, void *userdata, const uint8_t *data, size_t len) {
    ptyWrite(static_cast<TerminalSession *>(userdata)->ptyFd, (const char *)data, len);
}

bool effectSize(GhosttyTerminal, void *userdata, GhosttySizeReportSize *out) {
    ZoneScoped;
    auto *s = static_cast<TerminalSession *>(userdata);
    out->rows = s->rows;
    out->columns = s->cols;
    out->cell_width = (uint32_t)s->cellWidth;
    out->cell_height = (uint32_t)s->cellHeight;
    return true;
}

bool effectDeviceAttributes(GhosttyTerminal, void *, GhosttyDeviceAttributes *out) {
    ZoneScoped;
    out->primary.conformance_level = GHOSTTY_DA_CONFORMANCE_VT220;
    out->primary.features[0] = GHOSTTY_DA_FEATURE_COLUMNS_132;
    out->primary.features[1] = GHOSTTY_DA_FEATURE_SELECTIVE_ERASE;
    out->primary.features[2] = GHOSTTY_DA_FEATURE_ANSI_COLOR;
    out->primary.num_features = 3;
    out->secondary.device_type = GHOSTTY_DA_DEVICE_TYPE_VT220;
    out->secondary.firmware_version = 1;
    out->secondary.rom_cartridge = 0;
    out->tertiary.unit_id = 0;
    return true;
}

GhosttyString effectXtversion(GhosttyTerminal, void *) {
    ZoneScoped;
    GhosttyString name{};
    name.ptr = (const uint8_t *)"Kuru";
    name.len = 4;
    return name;
}

void effectTitleChanged(GhosttyTerminal terminal, void *userdata) {
    ZoneScoped;
    GhosttyString title{};
    if (ghostty_terminal_get(terminal, GHOSTTY_TERMINAL_DATA_TITLE, &title) != GHOSTTY_SUCCESS) {
        return;
    }
    static_cast<TerminalSession *>(userdata)->title.assign((const char *)title.ptr, title.len);
}

// No way to ask for the OS color scheme here, so the query is ignored.
bool effectColorScheme(GhosttyTerminal, void *, GhosttyColorScheme *) {
    return false;
}

std::unique_ptr<TerminalSession> openSession(int cellWidth, int cellHeight) {
    ZoneScoped;
    auto s = std::make_unique<TerminalSession>();
    s->cellWidth = cellWidth;
    s->cellHeight = cellHeight;
    auto ok = [](GhosttyResult result, const char *what) {
        if (result != GHOSTTY_SUCCESS) {
            fprintf(stderr, "terminal: %s failed (%d)\n", what, (int)result);
            return false;
        }
        return true;
    };
    if (!ok(ghostty_terminal_new(nullptr, &s->terminal, s->cols, s->rows), "ghostty_terminal_new")) {
        return nullptr;
    }
    // ~1000 lines; libghostty prunes whole pages, so it's a ballpark
    size_t scrollback = 1000;
    ghostty_terminal_set(s->terminal, GHOSTTY_TERMINAL_OPT_SCROLLBACK_MAX_LINES, &scrollback);
    ghostty_terminal_resize(s->terminal, s->cols, s->rows, (uint32_t)cellWidth, (uint32_t)cellHeight);
    if (!ptySpawn(*s)) {
        return nullptr;
    }
    // Unique_ptr: the session never moves, so it can be the effects' userdata.
    ghostty_terminal_set(s->terminal, GHOSTTY_TERMINAL_OPT_USERDATA, s.get());
    ghostty_terminal_set(s->terminal, GHOSTTY_TERMINAL_OPT_WRITE_PTY, (const void *)effectWritePty);
    ghostty_terminal_set(s->terminal, GHOSTTY_TERMINAL_OPT_SIZE, (const void *)effectSize);
    ghostty_terminal_set(s->terminal, GHOSTTY_TERMINAL_OPT_DEVICE_ATTRIBUTES, (const void *)effectDeviceAttributes);
    ghostty_terminal_set(s->terminal, GHOSTTY_TERMINAL_OPT_XTVERSION, (const void *)effectXtversion);
    ghostty_terminal_set(s->terminal, GHOSTTY_TERMINAL_OPT_TITLE_CHANGED, (const void *)effectTitleChanged);
    ghostty_terminal_set(s->terminal, GHOSTTY_TERMINAL_OPT_COLOR_SCHEME, (const void *)effectColorScheme);
    // ponytail: no Kitty graphics (ghostling re-uploads every image every frame); add
    // when needed: storage limit + PNG decoder + a Vulkan texture per image id
    if (!ok(ghostty_key_encoder_new(nullptr, &s->keyEncoder), "ghostty_key_encoder_new")
        || !ok(ghostty_key_event_new(nullptr, &s->keyEvent), "ghostty_key_event_new")
        || !ok(ghostty_mouse_encoder_new(nullptr, &s->mouseEncoder), "ghostty_mouse_encoder_new")
        || !ok(ghostty_mouse_event_new(nullptr, &s->mouseEvent), "ghostty_mouse_event_new")
        || !ok(ghostty_render_state_new(nullptr, &s->renderState), "ghostty_render_state_new")
        || !ok(ghostty_render_state_row_iterator_new(nullptr, &s->rowIter), "ghostty_render_state_row_iterator_new")
        || !ok(ghostty_render_state_row_cells_new(nullptr, &s->rowCells), "ghostty_render_state_row_cells_new")) {
        return nullptr;
    }
    return s;
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

struct KeyMap {
    GhosttyKey key;
    uint32_t codepoint; // unshifted, US layout; the Kitty protocol identifies keys by it
};

KeyMap mapKey(ImGuiKey k) {
    ZoneScoped;
    if (k >= ImGuiKey_A && k <= ImGuiKey_Z) {
        return {GhosttyKey(GHOSTTY_KEY_A + (k - ImGuiKey_A)), uint32_t('a' + (k - ImGuiKey_A))};
    }
    if (k >= ImGuiKey_0 && k <= ImGuiKey_9) {
        return {GhosttyKey(GHOSTTY_KEY_DIGIT_0 + (k - ImGuiKey_0)), uint32_t('0' + (k - ImGuiKey_0))};
    }
    if (k >= ImGuiKey_F1 && k <= ImGuiKey_F12) {
        return {GhosttyKey(GHOSTTY_KEY_F1 + (k - ImGuiKey_F1)), 0};
    }
    switch (k) {
    case ImGuiKey_Space:        return {GHOSTTY_KEY_SPACE, ' '};
    case ImGuiKey_Enter:        return {GHOSTTY_KEY_ENTER, 0};
    case ImGuiKey_KeypadEnter:  return {GHOSTTY_KEY_ENTER, 0};
    case ImGuiKey_Tab:          return {GHOSTTY_KEY_TAB, 0};
    case ImGuiKey_Backspace:    return {GHOSTTY_KEY_BACKSPACE, 0};
    case ImGuiKey_Delete:       return {GHOSTTY_KEY_DELETE, 0};
    case ImGuiKey_Escape:       return {GHOSTTY_KEY_ESCAPE, 0};
    case ImGuiKey_UpArrow:      return {GHOSTTY_KEY_ARROW_UP, 0};
    case ImGuiKey_DownArrow:    return {GHOSTTY_KEY_ARROW_DOWN, 0};
    case ImGuiKey_LeftArrow:    return {GHOSTTY_KEY_ARROW_LEFT, 0};
    case ImGuiKey_RightArrow:   return {GHOSTTY_KEY_ARROW_RIGHT, 0};
    case ImGuiKey_Home:         return {GHOSTTY_KEY_HOME, 0};
    case ImGuiKey_End:          return {GHOSTTY_KEY_END, 0};
    case ImGuiKey_PageUp:       return {GHOSTTY_KEY_PAGE_UP, 0};
    case ImGuiKey_PageDown:     return {GHOSTTY_KEY_PAGE_DOWN, 0};
    case ImGuiKey_Insert:       return {GHOSTTY_KEY_INSERT, 0};
    case ImGuiKey_Minus:        return {GHOSTTY_KEY_MINUS, '-'};
    case ImGuiKey_Equal:        return {GHOSTTY_KEY_EQUAL, '='};
    case ImGuiKey_LeftBracket:  return {GHOSTTY_KEY_BRACKET_LEFT, '['};
    case ImGuiKey_RightBracket: return {GHOSTTY_KEY_BRACKET_RIGHT, ']'};
    case ImGuiKey_Backslash:    return {GHOSTTY_KEY_BACKSLASH, '\\'};
    case ImGuiKey_Semicolon:    return {GHOSTTY_KEY_SEMICOLON, ';'};
    case ImGuiKey_Apostrophe:   return {GHOSTTY_KEY_QUOTE, '\''};
    case ImGuiKey_Comma:        return {GHOSTTY_KEY_COMMA, ','};
    case ImGuiKey_Period:       return {GHOSTTY_KEY_PERIOD, '.'};
    case ImGuiKey_Slash:        return {GHOSTTY_KEY_SLASH, '/'};
    case ImGuiKey_GraveAccent:  return {GHOSTTY_KEY_BACKQUOTE, '`'};
    default:                    return {GHOSTTY_KEY_UNIDENTIFIED, 0};
    }
}

GhosttyMods currentMods() {
    ZoneScoped;
    const ImGuiIO &io = ImGui::GetIO();
    GhosttyMods mods = 0;
    if (io.KeyShift) { mods |= GHOSTTY_MODS_SHIFT; }
    if (io.KeyCtrl) { mods |= GHOSTTY_MODS_CTRL; }
    if (io.KeyAlt) { mods |= GHOSTTY_MODS_ALT; }
    if (io.KeySuper) { mods |= GHOSTTY_MODS_SUPER; }
    return mods;
}

// The key encoder turns key events into whatever the program asked for
// (application cursor keys, Kitty keyboard protocol...), so there are no
// escape-sequence tables here.
void handleKeys(TerminalSession &s) {
    ZoneScoped;
    ghostty_key_encoder_setopt_from_terminal(s.keyEncoder, s.terminal);

    // This frame's typed text, taken so no other widget sees it too
    ImGuiIO &io = ImGui::GetIO();
    std::string text;
    for (ImWchar c : io.InputQueueCharacters) {
        char u8[5];
        text.append(u8, ImTextCharToUtf8(u8, c));
    }
    io.InputQueueCharacters.resize(0);

    GhosttyMods mods = currentMods();
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; k++) {
        ImGuiKey key = ImGuiKey(k);
        bool pressed = ImGui::IsKeyPressed(key, false);
        bool repeated = !pressed && ImGui::IsKeyPressed(key, true);
        bool released = ImGui::IsKeyReleased(key);
        if (!pressed && !repeated && !released) {
            continue;
        }
        KeyMap m = mapKey(key);
        if (m.key == GHOSTTY_KEY_UNIDENTIFIED) {
            continue;
        }
        GhosttyKeyAction action = GHOSTTY_KEY_ACTION_REPEAT;
        if (released) {
            action = GHOSTTY_KEY_ACTION_RELEASE;
        } else if (pressed) {
            action = GHOSTTY_KEY_ACTION_PRESS;
        }
        ghostty_key_event_set_key(s.keyEvent, m.key);
        ghostty_key_event_set_action(s.keyEvent, action);
        ghostty_key_event_set_mods(s.keyEvent, mods);
        ghostty_key_event_set_unshifted_codepoint(s.keyEvent, m.codepoint);
        // Shift is already in the text of a printable key ('a' -> 'A')
        GhosttyMods consumed = 0;
        if (m.codepoint != 0 && (mods & GHOSTTY_MODS_SHIFT)) {
            consumed |= GHOSTTY_MODS_SHIFT;
        }
        ghostty_key_event_set_consumed_mods(s.keyEvent, consumed);
        // The text rides on the first key event of the frame; releases carry none
        if (!text.empty() && !released) {
            ghostty_key_event_set_utf8(s.keyEvent, text.data(), text.size());
        } else {
            ghostty_key_event_set_utf8(s.keyEvent, nullptr, 0);
        }

        char buf[128];
        size_t written = 0;
        if (ghostty_key_encoder_encode(s.keyEncoder, s.keyEvent, buf, sizeof(buf), &written) == GHOSTTY_SUCCESS
            && written > 0) {
            ptyWrite(s.ptyFd, buf, written);
            text.clear();
        }
    }
    // Text whose key event came on another frame (or never did) still gets typed
    if (!text.empty()) {
        ptyWrite(s.ptyFd, text.data(), text.size());
    }
}

void mouseWrite(TerminalSession &s) {
    ZoneScoped;
    char buf[128];
    size_t written = 0;
    if (ghostty_mouse_encoder_encode(s.mouseEncoder, s.mouseEvent, buf, sizeof(buf), &written) == GHOSTTY_SUCCESS
        && written > 0) {
        ptyWrite(s.ptyFd, buf, written);
    }
}

// Mouse reporting through the encoder (it knows the tracking mode and format
// the program enabled); the wheel scrolls the scrollback when nothing tracks.
void handleMouse(TerminalSession &s, ImVec2 origin, ImVec2 size, bool hovered) {
    ZoneScoped;
    ghostty_mouse_encoder_setopt_from_terminal(s.mouseEncoder, s.terminal);
    GhosttyMouseEncoderSize encSize = GHOSTTY_INIT_SIZED(GhosttyMouseEncoderSize);
    encSize.screen_width = (uint32_t)size.x;
    encSize.screen_height = (uint32_t)size.y;
    encSize.cell_width = (uint32_t)s.cellWidth;
    encSize.cell_height = (uint32_t)s.cellHeight;
    encSize.padding_top = (uint32_t)pad;
    encSize.padding_bottom = (uint32_t)pad;
    encSize.padding_left = (uint32_t)pad;
    encSize.padding_right = (uint32_t)pad;
    ghostty_mouse_encoder_setopt(s.mouseEncoder, GHOSTTY_MOUSE_ENCODER_OPT_SIZE, &encSize);

    const GhosttyMouseButton buttons[] = {
        GHOSTTY_MOUSE_BUTTON_LEFT, GHOSTTY_MOUSE_BUTTON_RIGHT, GHOSTTY_MOUSE_BUTTON_MIDDLE}; // ImGuiMouseButton order
    bool anyPressed = false;
    for (int b = 0; b < 3; b++) {
        anyPressed = anyPressed || ImGui::IsMouseDown(b);
    }
    ghostty_mouse_encoder_setopt(s.mouseEncoder, GHOSTTY_MOUSE_ENCODER_OPT_ANY_BUTTON_PRESSED, &anyPressed);
    bool trackCell = true; // drop motion that stays inside one cell
    ghostty_mouse_encoder_setopt(s.mouseEncoder, GHOSTTY_MOUSE_ENCODER_OPT_TRACK_LAST_CELL, &trackCell);

    const ImGuiIO &io = ImGui::GetIO();
    GhosttyMousePosition pos{};
    pos.x = io.MousePos.x - origin.x;
    pos.y = io.MousePos.y - origin.y;
    ghostty_mouse_event_set_mods(s.mouseEvent, currentMods());
    ghostty_mouse_event_set_position(s.mouseEvent, pos);

    for (int b = 0; b < 3; b++) {
        if (hovered && ImGui::IsMouseClicked(b)) {
            ghostty_mouse_event_set_action(s.mouseEvent, GHOSTTY_MOUSE_ACTION_PRESS);
            ghostty_mouse_event_set_button(s.mouseEvent, buttons[b]);
            mouseWrite(s);
        } else if (ImGui::IsMouseReleased(b)) {
            ghostty_mouse_event_set_action(s.mouseEvent, GHOSTTY_MOUSE_ACTION_RELEASE);
            ghostty_mouse_event_set_button(s.mouseEvent, buttons[b]);
            mouseWrite(s);
        }
    }

    if (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f) {
        ghostty_mouse_event_set_action(s.mouseEvent, GHOSTTY_MOUSE_ACTION_MOTION);
        ghostty_mouse_event_clear_button(s.mouseEvent);
        for (int b = 2; b >= 0; b--) { // left wins, like ghostling
            if (ImGui::IsMouseDown(b)) {
                ghostty_mouse_event_set_button(s.mouseEvent, buttons[b]);
            }
        }
        mouseWrite(s);
    }

    if (hovered && io.MouseWheel != 0.0f) {
        bool tracking = false;
        ghostty_terminal_get(s.terminal, GHOSTTY_TERMINAL_DATA_MOUSE_TRACKING, &tracking);
        if (tracking) {
            GhosttyMouseButton wheelButton = GHOSTTY_MOUSE_BUTTON_FIVE;
            if (io.MouseWheel > 0.0f) {
                wheelButton = GHOSTTY_MOUSE_BUTTON_FOUR;
            }
            ghostty_mouse_event_set_button(s.mouseEvent, wheelButton);
            ghostty_mouse_event_set_action(s.mouseEvent, GHOSTTY_MOUSE_ACTION_PRESS);
            mouseWrite(s);
            ghostty_mouse_event_set_action(s.mouseEvent, GHOSTTY_MOUSE_ACTION_RELEASE);
            mouseWrite(s);
        } else {
            GhosttyTerminalScrollViewport sv{};
            sv.tag = GHOSTTY_SCROLL_VIEWPORT_DELTA;
            sv.value.delta = 3; // rows per tick, negative is up into history
            if (io.MouseWheel > 0.0f) {
                sv.value.delta = -3;
            }
            ghostty_terminal_scroll_viewport(s.terminal, sv);
        }
    }
}

// Dragging the right edge maps mouse y straight to a scroll offset. True
// while dragging, so the click doesn't also reach the program.
bool handleScrollbar(TerminalSession &s, ImVec2 origin, ImVec2 size, bool hovered) {
    ZoneScoped;
    GhosttyTerminalScrollbar bar{};
    if (ghostty_terminal_get(s.terminal, GHOSTTY_TERMINAL_DATA_SCROLLBAR, &bar) != GHOSTTY_SUCCESS) {
        return false;
    }
    if (bar.total <= bar.len) {
        s.scrollbarDragging = false;
        return false;
    }
    ImVec2 mouse = ImGui::GetIO().MousePos;
    float hitLeft = origin.x + size.x - barWidth - barMargin - 8.0f; // wider than the bar, easier to grab
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && mouse.x >= hitLeft) {
        s.scrollbarDragging = true;
    }
    if (s.scrollbarDragging && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        double frac = ImClamp((double)(mouse.y - origin.y) / (double)size.y, 0.0, 1.0);
        int64_t target = (int64_t)(frac * (double)(bar.total - bar.len));
        intptr_t delta = (intptr_t)(target - (int64_t)bar.offset);
        if (delta != 0) {
            GhosttyTerminalScrollViewport sv{};
            sv.tag = GHOSTTY_SCROLL_VIEWPORT_DELTA;
            sv.value.delta = delta;
            ghostty_terminal_scroll_viewport(s.terminal, sv);
        }
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        s.scrollbarDragging = false;
    }
    return s.scrollbarDragging;
}

// Focus in/out reports, only if the program turned them on (DECSET 1004)
void handleFocus(TerminalSession &s, bool focused) {
    ZoneScoped;
    if (focused == s.focused) {
        return;
    }
    s.focused = focused;
    GhosttyTerminalModeConfig mode{};
    mode.mode = GHOSTTY_MODE_FOCUS_EVENT;
    if (s.childExited
        || ghostty_terminal_get(s.terminal, GHOSTTY_TERMINAL_DATA_MODE, &mode) != GHOSTTY_SUCCESS
        || !mode.value) {
        return;
    }
    GhosttyFocusEvent event = GHOSTTY_FOCUS_LOST;
    if (focused) {
        event = GHOSTTY_FOCUS_GAINED;
    }
    char buf[8];
    size_t written = 0;
    if (ghostty_focus_encode(event, buf, sizeof(buf), &written) == GHOSTTY_SUCCESS && written > 0) {
        ptyWrite(s.ptyFd, buf, written);
    }
}

// Grid size from the content area: resizes the terminal (reflow) and the pty
// (so the shell gets SIGWINCH).
void handleResize(TerminalSession &s, ImVec2 size) {
    ZoneScoped;
    uint16_t cols = (uint16_t)ImMax(1, (int)((size.x - 2 * pad) / s.cellWidth));
    uint16_t rows = (uint16_t)ImMax(1, (int)((size.y - 2 * pad) / s.cellHeight));
    if (cols == s.cols && rows == s.rows) {
        return;
    }
    s.cols = cols;
    s.rows = rows;
    ghostty_terminal_resize(s.terminal, cols, rows, (uint32_t)s.cellWidth, (uint32_t)s.cellHeight);
    winsize ws = windowSize(s);
    ioctl(s.ptyFd, TIOCSWINSZ, &ws);
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

ImU32 rgb(GhosttyColorRgb c, int alpha = 255) {
    return IM_COL32(c.r, c.g, c.b, alpha);
}

// Cell by cell from the render state snapshot: backgrounds, glyphs (with
// fake bold/italic like ghostling), then cursor and scrollbar on top.
void render(TerminalSession &s, ImDrawList *dl, ImFont *font, ImVec2 origin, ImVec2 size) {
    ZoneScoped;
    GhosttyRenderStateColors colors = GHOSTTY_INIT_SIZED(GhosttyRenderStateColors);
    if (ghostty_render_state_colors_get(s.renderState, &colors) != GHOSTTY_SUCCESS) {
        return;
    }
    dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), rgb(colors.background));
    if (ghostty_render_state_get(s.renderState, GHOSTTY_RENDER_STATE_DATA_ROW_ITERATOR, &s.rowIter) != GHOSTTY_SUCCESS) {
        return;
    }

    float y = origin.y + pad;
    while (ghostty_render_state_row_iterator_next(s.rowIter)) {
        if (ghostty_render_state_row_get(s.rowIter, GHOSTTY_RENDER_STATE_ROW_DATA_CELLS, &s.rowCells) != GHOSTTY_SUCCESS) {
            continue;
        }
        float x = origin.x + pad;
        while (ghostty_render_state_row_cells_next(s.rowCells)) {
            ImVec2 cellMin(x, y);
            ImVec2 cellMax(x + s.cellWidth, y + s.cellHeight);
            x += s.cellWidth;

            uint32_t graphemeLen = 0;
            ghostty_render_state_row_cells_get(s.rowCells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_LEN, &graphemeLen);
            GhosttyColorRgb bg = colors.background;
            bool hasBg = ghostty_render_state_row_cells_get(s.rowCells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_BG_COLOR, &bg) == GHOSTTY_SUCCESS;
            if (graphemeLen == 0) {
                // no text, but an erase with a color set still paints
                if (hasBg) {
                    dl->AddRectFilled(cellMin, cellMax, rgb(bg));
                }
                continue;
            }

            // GRAPHEMES_BUF writes every codepoint, so size for all of them
            std::vector<uint32_t> codepoints(graphemeLen);
            ghostty_render_state_row_cells_get(s.rowCells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_BUF, codepoints.data());
            std::string text;
            for (uint32_t cp : codepoints) {
                char u8[5];
                text.append(u8, ImTextCharToUtf8(u8, cp));
            }

            GhosttyColorRgb fg = colors.foreground;
            ghostty_render_state_row_cells_get(s.rowCells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_FG_COLOR, &fg);
            GhosttyStyle style = GHOSTTY_INIT_SIZED(GhosttyStyle);
            ghostty_render_state_row_cells_get(s.rowCells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_STYLE, &style);
            if (style.inverse) {
                std::swap(fg, bg);
                hasBg = true;
            }
            if (hasBg) {
                dl->AddRectFilled(cellMin, cellMax, rgb(bg));
            }
            float italic = 0.0f;
            if (style.italic) {
                italic = fontSize / 6.0f;
            }
            dl->AddText(font, fontSize, ImVec2(cellMin.x + italic, cellMin.y), rgb(fg), text.c_str());
            if (style.bold) {
                dl->AddText(font, fontSize, ImVec2(cellMin.x + italic + 1.0f, cellMin.y), rgb(fg), text.c_str());
            }
        }
        bool clean = false;
        ghostty_render_state_row_set(s.rowIter, GHOSTTY_RENDER_STATE_ROW_OPTION_DIRTY, &clean);
        y += s.cellHeight;
    }

    bool cursorVisible = false;
    bool cursorInViewport = false;
    ghostty_render_state_get(s.renderState, GHOSTTY_RENDER_STATE_DATA_CURSOR_VISIBLE, &cursorVisible);
    ghostty_render_state_get(s.renderState, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_HAS_VALUE, &cursorInViewport);
    if (cursorVisible && cursorInViewport) {
        uint16_t cx = 0;
        uint16_t cy = 0;
        ghostty_render_state_get(s.renderState, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_X, &cx);
        ghostty_render_state_get(s.renderState, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_Y, &cy);
        GhosttyColorRgb cursor = colors.foreground;
        if (colors.cursor_has_value) {
            cursor = colors.cursor;
        }
        ImVec2 min(origin.x + pad + cx * s.cellWidth, origin.y + pad + cy * s.cellHeight);
        dl->AddRectFilled(min, ImVec2(min.x + s.cellWidth, min.y + s.cellHeight), rgb(cursor, 128));
        // IME candidate window goes next to the cursor
        GImGui->PlatformImeData.InputPos = min;
        GImGui->PlatformImeData.InputLineHeight = (float)s.cellHeight;
    }

    GhosttyTerminalScrollbar bar{};
    if (ghostty_terminal_get(s.terminal, GHOSTTY_TERMINAL_DATA_SCROLLBAR, &bar) == GHOSTTY_SUCCESS
        && bar.total > bar.len) {
        float thumbHeight = ImMax(10.0f, size.y * (float)((double)bar.len / (double)bar.total));
        float scroll = (float)((double)bar.offset / (double)(bar.total - bar.len)); // 0 = oldest
        float barX = origin.x + size.x - barWidth - barMargin;
        float thumbY = origin.y + scroll * (size.y - thumbHeight);
        dl->AddRectFilled(ImVec2(barX, thumbY), ImVec2(barX + barWidth, thumbY + thumbHeight), IM_COL32(200, 200, 200, 128));
    }

    if (s.childExited) {
        char msg[128];
        if (s.childExitStatus >= 0) {
            snprintf(msg, sizeof(msg), "[process exited with status %d]", s.childExitStatus);
        } else {
            snprintf(msg, sizeof(msg), "[process exited]");
        }
        ImVec2 msgSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, msg);
        float bannerTop = origin.y + size.y - msgSize.y - 8.0f;
        dl->AddRectFilled(ImVec2(origin.x, bannerTop), ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(0, 0, 0, 180));
        dl->AddText(font, fontSize, ImVec2(origin.x + (size.x - msgSize.x) / 2, bannerTop + 4.0f), IM_COL32_WHITE, msg);
    }

    GhosttyRenderStateDirty cleanState = GHOSTTY_RENDER_STATE_DIRTY_FALSE;
    ghostty_render_state_set(s.renderState, GHOSTTY_RENDER_STATE_OPTION_DIRTY, &cleanState);
}

// Drain the shell's output, then reap it once the pty closes (EOF can come
// before the child is waitable, so this retries every frame).
void pump(TerminalSession &s) {
    ZoneScoped;
    if (!s.childExited && !ptyRead(s)) {
        s.childExited = true;
    }
    if (s.childExited && !s.childReaped) {
        int status = 0;
        if (waitpid(s.child, &status, WNOHANG) > 0) {
            s.childReaped = true;
            if (WIFEXITED(status)) {
                s.childExitStatus = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                s.childExitStatus = 128 + WTERMSIG(status);
            }
        }
    }
}

} // namespace

TerminalPlugin::TerminalPlugin() { registerComponent<Terminal>(); }
TerminalPlugin::~TerminalPlugin() = default;

void TerminalPlugin::init(entt::registry &r) {
    ZoneScoped;
    ImGuiIO &io = ImGui::GetIO();
    font = io.Fonts->AddFontFromFileTTF(assetPath("fonts/mono/JetBrainsMono-Regular.ttf").c_str(), fontSize);
    // Trickling hands queued keys/text out a few per frame (held back on mouse
    // motion, fast repeats, non-char keys): typing backs up behind the frame
    // rate. Off, every frame gets the whole queue, like ghostling's raylib.
    // ponytail: global, so a down+up within one frame elsewhere in the UI is
    // flattened; read SDL events directly if that ever bites
    io.ConfigInputTrickleEventQueue = false;
}

void TerminalPlugin::update(entt::registry &r) {
    ZoneScoped;
    std::erase_if(sessions, [&](const auto &entry) {
        return !r.valid(entry.first) || !r.all_of<Terminal>(entry.first);
    });

    ImVec2 cell = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, "M");
    int cellWidth = ImMax(1, (int)cell.x);
    int cellHeight = ImMax(1, (int)cell.y);
    std::vector<entt::entity> closed;
    for (auto e : r.view<Terminal>()) {
        auto &session = sessions[e];
        if (!session) {
            session = openSession(cellWidth, cellHeight);
        }
        if (!session) {
            closed.push_back(e);
            continue;
        }
        TerminalSession &s = *session;
        pump(s);
        std::string label = s.title + "###terminal" + std::to_string(entt::to_integral(e));
        ImGui::SetNextWindowSize(ImVec2(80.0f * cellWidth + 2 * pad, 24.0f * cellHeight + 2 * pad + ImGui::GetFrameHeight()),
                                 ImGuiCond_FirstUseEver);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        bool open = true;
        bool visible = ImGui::Begin(label.c_str(), &open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar();
        if (visible) {
            ImVec2 origin = ImGui::GetCursorScreenPos();
            ImVec2 size = ImGui::GetContentRegionAvail();
            size.x = ImMax(size.x, 1.0f);
            size.y = ImMax(size.y, 1.0f);
            // Swallows clicks/drags so they don't move the window
            ImGui::InvisibleButton("grid", size,
                                   ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
            bool hovered = ImGui::IsItemHovered();
            bool focused = ImGui::IsWindowFocused();
            handleResize(s, size);
            handleFocus(s, focused);
            bool scrollbarDrag = handleScrollbar(s, origin, size, hovered);
            if (!s.childExited) {
                if (focused) {
                    // Keep game hotkeys out, and have the SDL backend turn on
                    // text input so typed characters arrive at all
                    ImGui::SetNextFrameWantCaptureKeyboard(true);
                    GImGui->PlatformImeData.WantTextInput = true;
                    handleKeys(s);
                }
                if (!scrollbarDrag && (hovered || ImGui::IsItemActive())) {
                    handleMouse(s, origin, size, hovered);
                }
            }
            pump(s); // pick up the echo of what was just typed, not a frame late
            ghostty_render_state_update(s.renderState, s.terminal);
            render(s, ImGui::GetWindowDrawList(), font, origin, size);
        }
        ImGui::End();
        if (!open) {
            closed.push_back(e);
        }
    }
    for (auto e : closed) {
        r.remove<Terminal>(e);
    }
}
