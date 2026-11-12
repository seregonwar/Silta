#include "silta/components/input.hpp"
#include "common.hpp"
#include "../per_context.hpp"
#include "../shaper.hpp"
#include "../text_edit.hpp"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>

namespace silta {
namespace {
using detail::CaretPosition;
using detail::EditCluster;
using detail::EditLayout;
using detail::EditLine;

struct Field {
  const char *placeholder = nullptr;
  float width = fill;
  float height = 36; // points
  float pad_y = 0;   // points; 0 centres one line vertically
  float pad_x = 12;  // points
  bool disabled = false;
  bool password = false;
  bool textarea = false;
  bool wrap = true;
  ImGuiInputTextFlags flags = 0;
};

struct Chrome {
  ImRect box;
  float radius = 0;
};

Chrome begin_field(const Field &f) {
  const Theme &t = theme();
  const float h = px(f.height);
  const float w = layout::resolve_width(f.width, px(240));
  const ImRect box = layout::allocate(ImVec2(w, h), f.width == fill);
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const float radius = px(t.radius_md());
  draw::shadow(dl, box, radius, draw::shadow_xs());
  draw::fill(dl, box, t.is_dark() ? t.input.fade(0.3f) : transparent, radius);
  return {box, radius};
}

void end_field(ImGuiID id, const Field &f, const Chrome &c, bool focused) {
  const Theme &t = theme();
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const float focus_t = anim::tween(anim::sub_id(id, "focus"), focused && !f.disabled ? 1.f : 0.f);
  draw::border(dl, c.box, mix(t.input, t.ring, focus_t), c.radius, px(1));
  if (focus_t > 0) draw::focus_ring(dl, c.box, c.radius, t.ring.fade(focus_t));
}

int resize_string(ImGuiInputTextCallbackData *data) {
  if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
    auto *str = static_cast<std::string *>(data->UserData);
    str->resize(size_t(data->BufTextLen));
    data->Buf = str->data();
  }
  return 0;
}

// ImGui's InputText does the editing, with its frame made invisible: Silta
// draws the box around it. Used for plain LTR text, password fields and builds
// without shaping.
template <class Edit>
bool imgui_edit(const char *id, const Field &f, const ImRect &box, Edit edit, bool &focused) {
  const Theme &t = theme();
  // Font size in pixels; ImGui multiplies the pushed size by the host's own
  // global scales, so divide them out (the host may use another scale).
  const float fs = imgui_font_size(font_points(TextSize::sm));
  const ImGuiStyle &style = ImGui::GetStyle();
  const float host_scale = std::max(1e-3f, style.FontScaleMain * style.FontScaleDpi);
  const float pad_y = f.pad_y > 0 ? px(f.pad_y) : std::floor((box.GetHeight() - fs) / 2);
  ImGui::PushFont(font_for(Weight::regular), fs / host_scale);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(px(12), pad_y));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
  ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, px(8));
  ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, px(4));
  // Silta's colours, whatever the host's ImGui style is.
  ImGui::PushStyleColor(ImGuiCol_Text, t.foreground.vec4());
  ImGui::PushStyleColor(ImGuiCol_TextDisabled, t.muted_foreground.vec4());
  ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, t.ring.fade(0.35f).vec4());
  ImGui::PushStyleColor(ImGuiCol_InputTextCursor, t.foreground.vec4());
  ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, t.border.vec4());
  ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, mix(t.border, t.muted_foreground, 0.4f).vec4());
  ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, t.muted_foreground.vec4());
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));

  const ImVec2 saved = ImGui::GetCursorScreenPos();
  ImGui::SetCursorScreenPos(box.Min);
  ImGui::SetNextItemWidth(box.GetWidth());
  const std::string label = std::string("##") + id;
  // Escape leaves the field and keeps the text, as browsers do (ImGui's
  // InputText would revert the edit).
  if (ImGui::GetActiveID() == ImGui::GetID(label.c_str()) && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    ImGui::ClearActiveID();
  const bool changed = edit(label.c_str(), box.GetSize());
  const ImGuiID item = ImGui::GetItemID();
  const bool active = ImGui::IsItemActive();
  ImGui::SetCursorScreenPos(saved);

  ImGui::PopStyleColor(13);
  ImGui::PopStyleVar(4);
  ImGui::PopFont();

  const ImGuiContext &g = *GImGui;
  focused = !f.disabled && (active || (g.NavId == item && g.NavCursorVisible));
  return changed;
}

// ------------------------------------------------------------ shaped editor
struct EditorState {
  std::size_t cursor = 0;
  std::size_t anchor = 0;
  int line = 0;
  float x = 0;
  float scroll_x = 0;
  float scroll_y = 0;
  double blink = 0;
  int frame = 0;
  bool initialized = false;
  detail::ImeEdit ime;
};

struct EditorStore {
  std::unordered_map<ImGuiID, EditorState> states;
};

EditorStore &editor_store() { return detail::per_context<EditorStore>(); }

// What the field should size itself to this frame. A composition preview is
// laid out exactly like the committed text, so a wrapping Textarea grows with
// it instead of clipping the preedit.
std::string composition_display(ImGuiID id, const std::string &text) {
  EditorState &st = editor_store().states[id];
  return st.ime.active ? detail::ime_preview_text(text, st.ime) : text;
}

// A host IME (or the scripted harness) deposits the latest composition here.
// The focused shaped field consumes it on its next frame. `focused` is the
// last shaped field that was active; `owner` is the field the composition
// belongs to, which may differ for one frame while focus moves.
struct ImeBridge {
  ImGuiID focused = 0;
  ImGuiID owner = 0;
  std::string preview;
  bool active = false;
  bool commit = false;
  bool cancel = false;
};

ImeBridge &ime_bridge() { return detail::per_context<ImeBridge>(); }

void clear_ime_request(ImeBridge &b) {
  b.owner = 0;
  b.preview.clear();
  b.active = false;
  b.commit = false;
  b.cancel = false;
}

std::size_t clamp_offset(const std::string &text, std::size_t pos) {
  pos = std::min(pos, text.size());
  while (pos > 0 && pos < text.size() && (static_cast<unsigned char>(text[pos]) & 0xC0) == 0x80) --pos;
  return pos;
}

bool filter_char(unsigned int &c, ImGuiInputTextFlags flags) {
  if (flags & ImGuiInputTextFlags_CharsDecimal)
    if (!((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+' || c == '*' || c == '/')) return false;
  if (flags & ImGuiInputTextFlags_CharsHexadecimal)
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
  if (flags & ImGuiInputTextFlags_CharsNoBlank)
    if (c == ' ' || c == '\t') return false;
  if (flags & ImGuiInputTextFlags_CharsUppercase)
    if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
  return true;
}

std::string sanitize_insert(const std::string &input, const Field &f) {
  const std::string normalized = detail::sanitize_paste(input, f.textarea);
  std::string out;
  out.reserve(normalized.size());
  for (std::size_t i = 0; i < normalized.size();) {
    unsigned int c = 0;
    const int n = ImTextCharFromUtf8(&c, normalized.c_str() + i, normalized.c_str() + normalized.size());
    if (n <= 0) break;
    i += std::size_t(n);
    if (!filter_char(c, f.flags)) continue;
    out += detail::encode_utf8(c);
  }
  return out;
}

bool has_selection(const EditorState &st) { return st.cursor != st.anchor; }

struct ShapedResult {
  bool changed = false;
  bool focused = false;
};

ShapedResult shaped_edit(ImGuiID id, const Field &f, const ImRect &box, std::string &text) {
  const Theme &t = theme();
  ImGuiIO &io = ImGui::GetIO();
  ImGuiContext &g = *GImGui;
  const float fs = imgui_font_size(font_points(TextSize::sm));
  const float pad_x = px(f.pad_x);
  const float pad_y = f.pad_y > 0 ? px(f.pad_y) : std::floor((box.GetHeight() - fs) / 2);
  const ImRect content(ImVec2(box.Min.x + pad_x, box.Min.y + pad_y),
                       ImVec2(std::max(box.Min.x + pad_x + 1.f, box.Max.x - pad_x),
                              std::max(box.Min.y + pad_y + 1.f, box.Max.y - pad_y)));
  const float cw = content.GetWidth();
  const float ch = content.GetHeight();
  const bool rtl = is_rtl();
  const bool multiline = f.textarea;
  const bool wrap = multiline && f.wrap;
  const bool readonly = (f.flags & ImGuiInputTextFlags_ReadOnly) != 0;

  EditorState &st = editor_store().states[id];
  st.frame = ImGui::GetFrameCount();
  if (!st.initialized) {
    st.cursor = st.anchor = text.size();
    st.initialized = true;
  }
  st.cursor = clamp_offset(text, st.cursor);
  st.anchor = clamp_offset(text, st.anchor);

  ImGui::ItemAdd(box, id);
  // Allow clicking straight from one active field into another: ImGui's
  // default IsWindowHovered() reports false while any item is active.
  const bool hovered = !f.disabled && ImGui::IsMouseHoveringRect(box.Min, box.Max) &&
                       ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
  if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
  bool active = g.ActiveId == id;
  bool changed = false;
  bool entered = false;
  ImeBridge &bridge = ime_bridge();
  if (active) bridge.focused = id;
  else if (bridge.focused == id && !st.ime.active) bridge.focused = 0;

  // A composition belongs to the field where it began. If focus moved away,
  // commit it here rather than dropping the preview.
  if (st.ime.active && (!active || (bridge.owner != 0 && bridge.owner != id))) {
    if (bridge.owner == id && bridge.cancel) {
      detail::ime_cancel(st.ime);
      clear_ime_request(bridge);
    } else {
      changed |= detail::ime_commit(text, st.ime, st.cursor, st.anchor);
      if (bridge.owner == id) clear_ime_request(bridge);
    }
  }
  if (bridge.owner == id && !readonly) {
    if (bridge.cancel) {
      detail::ime_cancel(st.ime);
      clear_ime_request(bridge);
    } else if (bridge.commit) {
      // A result string can arrive in the same frame as (or before) its
      // preview, so seed the composition from the request before committing.
      if (!st.ime.active && !bridge.preview.empty())
        detail::ime_begin(st.ime, bridge.preview, st.cursor, st.anchor);
      changed |= detail::ime_commit(text, st.ime, st.cursor, st.anchor);
      clear_ime_request(bridge);
    } else if (bridge.active) {
      if (!active) clear_ime_request(bridge);
      else if (!st.ime.active) detail::ime_begin(st.ime, bridge.preview, st.cursor, st.anchor);
      else detail::ime_update(st.ime, bridge.preview);
    }
  }
  if (readonly && bridge.owner == id) clear_ime_request(bridge);

  std::string display = st.ime.active ? detail::ime_preview_text(text, st.ime) : text;
  EditLayout layout;
  layout_edit_text(display.c_str(), display.size(), 0, fs, rtl, wrap, cw, cw, layout);
  CaretPosition caret = detail::caret_at(
      layout, st.ime.active ? detail::ime_preview_caret(st.ime) : st.cursor, st.x, st.line);
  st.line = caret.line;
  st.x = caret.x;
  if (active) {
    // Claim the keys ImGui's navigation would otherwise consume (and use to
    // scroll the page) while this field is active.
    g.ActiveIdUsingAllKeyboardKeys = true;
    g.ActiveIdUsingNavDirMask |= (1 << ImGuiDir_Left) | (1 << ImGuiDir_Right) | (1 << ImGuiDir_Up) |
                                 (1 << ImGuiDir_Down);
    for (ImGuiKey key : {ImGuiKey_LeftArrow, ImGuiKey_RightArrow, ImGuiKey_UpArrow, ImGuiKey_DownArrow,
                         ImGuiKey_Home, ImGuiKey_End, ImGuiKey_Backspace, ImGuiKey_Delete})
      ImGui::SetKeyOwner(key, id);
  }

  auto layout_x = [&](const ImVec2 &mouse) {
    return (mouse.x - content.Min.x) + (rtl ? -st.scroll_x : st.scroll_x);
  };
  auto layout_y = [&](const ImVec2 &mouse) { return (mouse.y - content.Min.y) + st.scroll_y; };
  auto set_caret = [&](const CaretPosition &p, bool extend) {
    st.cursor = p.offset;
    st.line = p.line;
    st.x = p.x;
    if (!extend) st.anchor = p.offset;
    st.blink = ImGui::GetTime();
  };
  auto relayout = [&] {
    display = st.ime.active ? detail::ime_preview_text(text, st.ime) : text;
    layout_edit_text(display.c_str(), display.size(), 0, fs, rtl, wrap, cw, cw, layout);
    if (st.ime.active) {
      const CaretPosition p = detail::caret_at(layout, detail::ime_preview_caret(st.ime), st.x, -1);
      st.line = p.line;
      st.x = p.x;
    } else {
      const CaretPosition p = detail::caret_at(layout, st.cursor, st.x, -1);
      st.cursor = p.offset;
      st.line = p.line;
      st.x = p.x;
    }
  };
  auto ensure_visible = [&] {
    const CaretPosition p = detail::caret_at(
        layout, st.ime.active ? detail::ime_preview_caret(st.ime) : st.cursor, st.x, st.line);
    st.line = p.line;
    st.x = p.x;
    const float margin = 2.f;
    const float vx = rtl ? p.x + st.scroll_x : p.x - st.scroll_x;
    if (vx < margin) st.scroll_x += rtl ? (margin - vx) : (vx - margin);
    else if (vx > cw - margin) st.scroll_x += rtl ? (vx - (cw - margin)) : (vx - (cw - margin));
    st.scroll_x = std::clamp(st.scroll_x, 0.f, std::max(0.f, layout.width - cw));
    if (multiline) {
      const float top = float(p.line) * layout.line_height - st.scroll_y;
      if (top < 0) st.scroll_y += top;
      else if (top + layout.line_height > ch) st.scroll_y += top + layout.line_height - ch;
      st.scroll_y = std::clamp(st.scroll_y, 0.f, std::max(0.f, layout.height - ch));
    } else {
      st.scroll_y = 0;
    }
  };

  // Focus and pointer.
  if (!f.disabled && hovered && io.MouseClicked[0]) {
    if (st.ime.active) {
      changed |= detail::ime_commit(text, st.ime, st.cursor, st.anchor);
      clear_ime_request(bridge);
      relayout();
    }
    ImGui::SetActiveID(id, ImGui::GetCurrentWindow());
    active = true;
    const CaretPosition p = detail::caret_at_point(layout, layout_x(io.MousePos), layout_y(io.MousePos));
    if (io.KeyShift) st.anchor = st.cursor;
    else st.anchor = p.offset;
    st.cursor = p.offset;
    st.line = p.line;
    st.x = p.x;
    st.blink = ImGui::GetTime();
    st.scroll_y = std::clamp(st.scroll_y, 0.f, std::max(0.f, layout.height - ch));
    ensure_visible();
    if (f.flags & ImGuiInputTextFlags_AutoSelectAll) {
      st.anchor = 0;
      st.cursor = text.size();
      ensure_visible();
    }
  }
  if (active && io.MouseClicked[0] && !hovered) {
    if (st.ime.active) {
      changed |= detail::ime_commit(text, st.ime, st.cursor, st.anchor);
      clear_ime_request(bridge);
      relayout();
    }
    ImGui::ClearActiveID();
    active = false;
  }
  if (active && io.MouseDown[0] && (io.MouseDelta.x != 0.f || io.MouseDelta.y != 0.f)) {
    const CaretPosition p = detail::caret_at_point(layout, layout_x(io.MousePos), layout_y(io.MousePos));
    st.cursor = p.offset;
    st.line = p.line;
    st.x = p.x;
    if (!io.KeyShift) st.anchor = p.offset;
    st.blink = ImGui::GetTime();
    ensure_visible();
  }

  if (active && !f.disabled) {
    bool ime_consumed = false;
    if (st.ime.active) {
      if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        detail::ime_cancel(st.ime);
        clear_ime_request(bridge);
        ime_consumed = true;
        relayout();
        ensure_visible();
      } else {
        const bool enter_key = ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                               ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
        const bool navigate = ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true) ||
                              ImGui::IsKeyPressed(ImGuiKey_RightArrow, true) ||
                              ImGui::IsKeyPressed(ImGuiKey_UpArrow, true) ||
                              ImGui::IsKeyPressed(ImGuiKey_DownArrow, true) ||
                              ImGui::IsKeyPressed(ImGuiKey_Home, false) ||
                              ImGui::IsKeyPressed(ImGuiKey_End, false) ||
                              ImGui::IsKeyPressed(ImGuiKey_Backspace, true) ||
                              ImGui::IsKeyPressed(ImGuiKey_Delete, true);
        if (enter_key || navigate) {
          changed |= detail::ime_commit(text, st.ime, st.cursor, st.anchor);
          clear_ime_request(bridge);
          relayout();
          ensure_visible();
          // A platform may also deliver the result as characters; the
          // composition commit already inserted it.
          io.InputQueueCharacters.resize(0);
          if (enter_key) ime_consumed = true;
        } else {
          // The preview owns ordinary typing until it commits or cancels.
          ime_consumed = true;
        }
      }
    }
    if (!ime_consumed) {
    // Typed characters.
    if (!readonly && !(io.KeyCtrl && !io.KeyAlt) && io.InputQueueCharacters.Size > 0) {
      for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
        unsigned int c = static_cast<unsigned int>(io.InputQueueCharacters[i]);
        if (c == 0) continue;
        if (!f.textarea && (c == '\n' || c == '\r')) continue;
        if (!filter_char(c, f.flags)) continue;
        detail::replace_selection(text, st.cursor, st.anchor, detail::encode_utf8(c));
        changed = true;
      }
      io.InputQueueCharacters.resize(0);
      if (changed) relayout();
    }

    const bool shift = io.KeyShift;
    auto collapse_selection = [&](int dir) {
      if (!has_selection(st)) return;
      const CaretPosition a = detail::caret_at(layout, st.cursor, st.x, -1);
      const CaretPosition b = detail::caret_at(layout, st.anchor, st.x, -1);
      const bool cursor_left = a.x < b.x;
      st.cursor = (dir < 0) == cursor_left ? st.cursor : st.anchor;
      st.anchor = st.cursor;
      const CaretPosition p = detail::caret_at(layout, st.cursor, st.x, -1);
      st.line = p.line;
      st.x = p.x;
    };
    auto move = [&](int dir) {
      const CaretPosition from = detail::caret_at(layout, st.cursor, st.x, st.line);
      const CaretPosition to = detail::move_caret(layout, from, dir);
      if (to.offset == from.offset && to.line == from.line) return;
      set_caret(to, shift);
      ensure_visible();
    };
    auto move_v = [&](int dir) {
      const CaretPosition from = detail::caret_at(layout, st.cursor, st.x, st.line);
      const CaretPosition to = detail::move_line(layout, from, dir);
      set_caret(to, shift);
      ensure_visible();
    };

    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true)) {
      if (!shift && has_selection(st)) collapse_selection(-1);
      else move(-1);
    } else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) {
      if (!shift && has_selection(st)) collapse_selection(1);
      else move(1);
    } else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) {
      move_v(-1);
    } else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) {
      move_v(1);
    } else if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) {
      CaretPosition p;
      if (io.KeyCtrl) p = detail::caret_at(layout, 0, st.x, -1);
      else {
        const EditLine &line = layout.lines[size_t(std::clamp(st.line, 0, int(layout.lines.size()) - 1))];
        p = detail::caret_at(layout, line.begin, st.x, st.line);
      }
      set_caret(p, shift);
      ensure_visible();
    } else if (ImGui::IsKeyPressed(ImGuiKey_End, false)) {
      CaretPosition p;
      if (io.KeyCtrl) p = detail::caret_at(layout, text.size(), st.x, -1);
      else {
        const EditLine &line = layout.lines[size_t(std::clamp(st.line, 0, int(layout.lines.size()) - 1))];
        p = detail::caret_at(layout, line.end, st.x, st.line);
      }
      set_caret(p, shift);
      ensure_visible();
    } else if (ImGui::IsKeyPressed(ImGuiKey_Backspace, true)) {
      if (!readonly) {
        bool edited = false;
        if (has_selection(st)) {
          detail::erase_selection(text, st.cursor, st.anchor);
          edited = true;
        } else {
          const CaretPosition from = detail::caret_at(layout, st.cursor, st.x, st.line);
          std::size_t a = 0, b = 0;
          if (detail::visual_delete_range(text.c_str(), text.size(), layout, from, -1, a, b)) {
            text.erase(a, b - a);
            st.cursor = st.anchor = a;
            edited = true;
          }
        }
        if (edited) {
          changed = true;
          relayout();
        }
      }
    } else if (ImGui::IsKeyPressed(ImGuiKey_Delete, true)) {
      if (!readonly) {
        bool edited = false;
        if (has_selection(st)) {
          detail::erase_selection(text, st.cursor, st.anchor);
          edited = true;
        } else {
          const CaretPosition from = detail::caret_at(layout, st.cursor, st.x, st.line);
          std::size_t a = 0, b = 0;
          if (detail::visual_delete_range(text.c_str(), text.size(), layout, from, 1, a, b)) {
            text.erase(a, b - a);
            st.cursor = st.anchor = a;
            edited = true;
          }
        }
        if (edited) {
          changed = true;
          relayout();
        }
      }
    }

    const bool ctrl = io.KeyCtrl && !io.KeyAlt;
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) {
      st.anchor = 0;
      st.cursor = text.size();
      ensure_visible();
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C, false) && has_selection(st)) {
      ImGui::SetClipboardText(detail::selection_text(text, st.cursor, st.anchor).c_str());
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_X, false) && has_selection(st) && !readonly) {
      ImGui::SetClipboardText(detail::selection_text(text, st.cursor, st.anchor).c_str());
      detail::erase_selection(text, st.cursor, st.anchor);
      changed = true;
      relayout();
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_V, false) && !readonly) {
      if (const char *clip = ImGui::GetClipboardText()) {
        const std::string insert = sanitize_insert(clip, f);
        if (!insert.empty()) {
          detail::replace_selection(text, st.cursor, st.anchor, insert);
          changed = true;
          relayout();
        }
      }
    }

    const bool ctrl_enter = ctrl && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                                     ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
    const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                       ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
    const bool shift_enter = io.KeyShift && enter;
    if (multiline) {
      const bool ctrl_newline = (f.flags & ImGuiInputTextFlags_CtrlEnterForNewLine) != 0;
      const bool newline_key = shift_enter || (ctrl_newline ? ctrl_enter : (enter && !ctrl_enter));
      const bool validate_key = ctrl_newline ? (enter && !ctrl_enter) : ctrl_enter;
      if (newline_key && !readonly) {
        detail::replace_selection(text, st.cursor, st.anchor, "\n");
        changed = true;
        relayout();
      } else if (validate_key) {
        entered = true;
        ImGui::ClearActiveID();
        active = false;
      }
    } else if (enter) {
      entered = true;
      ImGui::ClearActiveID();
      active = false;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
      ImGui::ClearActiveID();
      active = false;
    }
    } // !ime_consumed
    if (active) {
      io.WantTextInput = true;
      ensure_visible();
    }
  }

  // ---------------------------------------------------------------- drawing
  if (changed) ImGui::MarkItemEdited(id);
  const bool small = content.GetWidth() <= 1.f || content.GetHeight() <= 1.f;
  if (!small) {
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(content.Min, content.Max, true);
    const std::size_t sel_begin = std::min(st.cursor, st.anchor), sel_end = std::max(st.cursor, st.anchor);
    if (!st.ime.active && sel_begin < sel_end) {
      const ImU32 selected = t.ring.fade(0.35f).u32();
      const float shift_x = rtl ? st.scroll_x : -st.scroll_x;
      for (int i = 0; i < int(layout.lines.size()); ++i) {
        const EditLine &line = layout.lines[size_t(i)];
        const float y = content.Min.y + float(i) * layout.line_height - st.scroll_y;
        if (y + layout.line_height < content.Min.y || y > content.Max.y) continue;
        bool any = false;
        for (const EditCluster &c : line.clusters) {
          if (c.begin >= sel_end || c.end <= sel_begin) continue;
          const float x0 = content.Min.x + shift_x + line.origin + c.x0;
          const float x1 = content.Min.x + shift_x + line.origin + c.x1;
          dl->AddRectFilled(ImVec2(std::floor(x0), std::floor(y)), ImVec2(std::ceil(x1), std::ceil(y + layout.line_height)),
                            selected);
          any = true;
        }
        // A selected newline (or an empty line) needs a visible block too.
        const bool newline_selected =
            line.end < text.size() && text[line.end] == '\n' && sel_begin <= line.end && sel_end > line.end;
        if (!any && (newline_selected || (line.begin == line.end && sel_begin <= line.begin && sel_end > line.begin))) {
          const float edge = rtl ? line.origin : line.origin + line.width;
          const float x = content.Min.x + shift_x + edge;
          const float w = std::max(2.f, fs * 0.5f);
          dl->AddRectFilled(ImVec2(std::floor(rtl ? x - w : x), std::floor(y)),
                            ImVec2(std::ceil(rtl ? x : x + w), std::ceil(y + layout.line_height)), selected);
        }
      }
    }
    // Text.
    const float shift_x = rtl ? st.scroll_x : -st.scroll_x;
    for (int i = 0; i < int(layout.lines.size()); ++i) {
      const EditLine &line = layout.lines[size_t(i)];
      const float y = content.Min.y + float(i) * layout.line_height - st.scroll_y;
      if (y + layout.line_height < content.Min.y || y > content.Max.y) continue;
      const float x = content.Min.x + shift_x + line.origin;
      detail::draw_shaped(dl, ImVec2(std::floor(x), std::floor(y)), display.c_str() + line.begin,
                          display.c_str() + line.end, 0, fs, t.foreground.u32(), rtl);
      if (st.ime.active && !st.ime.preview.empty()) {
        // Underline the preview so an in-progress composition reads as such
        // while still being shaped with its surrounding text.
        const std::size_t pb = std::min(st.ime.cursor, st.ime.anchor);
        const std::size_t pe = pb + st.ime.preview.size();
        const ImU32 underline = t.ring.fade(0.9f).u32();
        for (const EditCluster &c : line.clusters) {
          if (c.end <= pb || c.begin >= pe) continue;
          const float x0 = content.Min.x + shift_x + line.origin + c.x0;
          const float x1 = content.Min.x + shift_x + line.origin + c.x1;
          dl->AddLine(ImVec2(std::floor(std::min(x0, x1)), std::floor(y + layout.line_height - 1.f)),
                      ImVec2(std::ceil(std::max(x0, x1)), std::floor(y + layout.line_height - 1.f)), underline,
                      1.f);
        }
      }
    }
    // Placeholder.
    if (text.empty() && !active && f.placeholder) {
      draw_text(dl, content.Min, f.placeholder, TextSize::sm, Weight::regular, t.muted_foreground, cw,
                fs / std::max(1e-3f, scale()), TextAlign::start);
    }
    // Caret.
    if (active && !f.disabled) {
      const CaretPosition p = detail::caret_at(
          layout, st.ime.active ? detail::ime_preview_caret(st.ime) : st.cursor, st.x, st.line);
      const float x = content.Min.x + shift_x + p.x;
      const float y = content.Min.y + float(p.line) * layout.line_height - st.scroll_y;
      // Put the platform IME candidate window at the shaped caret, including
      // when the preview itself is what is being composed.
      g.PlatformImeData.WantVisible = true;
      g.PlatformImeData.WantTextInput = true;
      g.PlatformImeData.InputPos = ImVec2(std::floor(x), std::floor(y));
      g.PlatformImeData.InputLineHeight = layout.line_height;
      const double phase = std::fmod(ImGui::GetTime() - st.blink, 1.2);
      const bool visible = !io.ConfigInputTextCursorBlink || phase <= 0.8;
      if (visible)
        dl->AddRectFilled(ImVec2(std::floor(x), std::floor(y + 1.f)),
                          ImVec2(std::floor(x) + 1.f, std::floor(y + layout.line_height - 1.f)),
                          t.foreground.u32());
    }
    dl->PopClipRect();
  }

  // Drop old per-field state now and then.
  if (ImGui::GetFrameCount() % 120 == 0) {
    for (auto it = editor_store().states.begin(); it != editor_store().states.end();) {
      if (ImGui::GetFrameCount() - it->second.frame > 600) it = editor_store().states.erase(it);
      else ++it;
    }
  }

  const bool report = (f.flags & ImGuiInputTextFlags_EnterReturnsTrue) ? entered : changed;
  return {report, active && !f.disabled};
}

bool shaped_for(const char *text, bool password) {
  return !password && detail::shaped_editing_available() && detail::text_needs_shaped_editor(text, is_rtl());
}
} // namespace

bool Input(const char *id, char *buffer, std::size_t buffer_size, const InputOptions &o) {
  detail::DisabledScope disabled(o.disabled);
  Field f;
  f.placeholder = o.placeholder;
  f.width = o.width;
  f.height = 36;
  f.disabled = o.disabled;
  f.password = o.password;
  f.flags = o.flags;
  const ImGuiID item = ImGui::GetID((std::string("##") + id).c_str());
  const bool shaped = shaped_for(buffer, o.password);
  Chrome chrome = begin_field(f);
  bool changed = false, focused = false;
  if (shaped) {
    std::string temp = buffer ? std::string(buffer, std::strlen(buffer)) : std::string();
    const ShapedResult r = shaped_edit(item, f, chrome.box, temp);
    if (r.changed && temp.size() + 1 <= buffer_size) {
      std::memcpy(buffer, temp.c_str(), temp.size() + 1);
      changed = true;
    }
    focused = r.focused;
  } else {
    changed = imgui_edit(id, f, chrome.box,
                         [&](const char *label_text, ImVec2) {
                           return ImGui::InputTextWithHint(label_text, f.placeholder ? f.placeholder : "", buffer,
                                                           buffer_size, f.flags | (o.password ? ImGuiInputTextFlags_Password : 0));
                         },
                         focused);
  }
  end_field(item, f, chrome, focused);
  return changed;
}

bool Input(const char *id, std::string *text, const InputOptions &o) {
  detail::DisabledScope disabled(o.disabled);
  Field f;
  f.placeholder = o.placeholder;
  f.width = o.width;
  f.height = 36;
  f.disabled = o.disabled;
  f.password = o.password;
  f.flags = o.flags;
  const ImGuiID item = ImGui::GetID((std::string("##") + id).c_str());
  const bool shaped = shaped_for(text->c_str(), o.password);
  Chrome chrome = begin_field(f);
  bool changed = false, focused = false;
  if (shaped) {
    const ShapedResult r = shaped_edit(item, f, chrome.box, *text);
    changed = r.changed;
    focused = r.focused;
  } else {
    changed = imgui_edit(id, f, chrome.box,
                         [&](const char *label_text, ImVec2) {
                           return ImGui::InputTextWithHint(label_text, f.placeholder ? f.placeholder : "", text->data(),
                                                           text->capacity() + 1,
                                                           f.flags | (o.password ? ImGuiInputTextFlags_Password : 0) |
                                                               ImGuiInputTextFlags_CallbackResize,
                                                           resize_string, text);
                         },
                         focused);
  }
  end_field(item, f, chrome, focused);
  return changed;
}

bool Textarea(const char *id, std::string *text, const TextareaOptions &o) {
  detail::DisabledScope disabled(o.disabled);
  const float fs = imgui_font_size(font_points(TextSize::sm));
  const float width = layout::resolve_width(o.width, px(240));
  const float pad_x = px(12), pad_y = 8;
  const ImGuiID item = ImGui::GetID((std::string("##") + id).c_str());
  const bool shaped = shaped_for(text->c_str(), false);
  float wanted = o.min_height;
  if (shaped) {
    const float wrap_width = o.wrap ? std::max(1.f, width - 2 * pad_x) : 0.f;
    const std::string display = composition_display(item, *text);
    const float content =
        detail::edit_text_height(display.c_str(), display.size(), 0, fs, is_rtl(), o.wrap, wrap_width);
    wanted = std::clamp(content / scale() + 2 * pad_y + 2, o.min_height, std::max(o.min_height, o.max_height));
  } else {
    const float wrap = o.wrap ? std::max(1.f, width - 2 * pad_x - px(8)) : 0.f;
    ImFont *font = font_for(Weight::regular);
    const char *s = text->c_str();
    float content = font->CalcTextSizeA(fs, FLT_MAX, wrap, s, s + text->size()).y;
    if (!text->empty() && text->back() == '\n') content += fs;
    wanted = std::clamp(content / scale() + 2 * pad_y + 2, o.min_height, std::max(o.min_height, o.max_height));
  }

  Field f;
  f.placeholder = o.placeholder;
  f.width = o.width;
  f.height = wanted;
  f.pad_y = pad_y;
  f.pad_x = 12;
  f.disabled = o.disabled;
  f.textarea = true;
  f.wrap = o.wrap;
  f.flags = o.flags;
  Chrome chrome = begin_field(f);
  bool changed = false, focused = false;
  if (shaped) {
    const ShapedResult r = shaped_edit(item, f, chrome.box, *text);
    changed = r.changed;
    focused = r.focused;
  } else {
    bool show_placeholder = false;
    ImRect placed;
    changed = imgui_edit(id, f, chrome.box,
                         [&](const char *label_text, ImVec2 size) {
                           const bool r = ImGui::InputTextMultiline(
                               label_text, text->data(), text->capacity() + 1, size,
                               f.flags | ImGuiInputTextFlags_CallbackResize | (o.wrap ? ImGuiInputTextFlags_WordWrap : 0),
                               resize_string, text);
                           placed = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
                           show_placeholder = text->empty() && !ImGui::IsItemActive();
                           return r;
                         },
                         focused);
    if (show_placeholder && o.placeholder)
      draw_text(ImGui::GetWindowDrawList(), ImVec2(placed.Min.x + pad_x, placed.Min.y + px(pad_y)), o.placeholder,
                TextSize::sm, Weight::regular, theme().muted_foreground,
                o.wrap ? placed.GetWidth() - 2 * pad_x : 0.f, fs / scale());
  }
  end_field(item, f, chrome, focused);
  return changed;
}

bool ime_composition(const char *utf8) {
  if (!utf8) return false;
  ImeBridge &b = ime_bridge();
  if (b.focused == 0) return false;
  b.owner = b.focused;
  b.preview = utf8;
  b.active = true;
  b.commit = false;
  b.cancel = false;
  return true;
}

bool ime_commit() {
  ImeBridge &b = ime_bridge();
  if (b.owner == 0) return false;
  b.commit = true;
  b.cancel = false;
  return true;
}

bool ime_cancel() {
  ImeBridge &b = ime_bridge();
  if (b.owner == 0) return false;
  b.cancel = true;
  b.commit = false;
  return true;
}

} // namespace silta
