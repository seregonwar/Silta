#pragma once
// Layout and navigation for text fields whose content needs shaping. The
// editor keeps byte offsets (as ImGui and std::string do) and maps them to
// visual positions with the Unicode bidi algorithm, so the caret, selection
// and arrow keys follow what the user sees instead of logical storage order.
#include <cstddef>
#include <string>
#include <vector>

namespace silta::detail {

struct EditCluster {
  std::size_t begin = 0, end = 0; // byte offsets in the whole text
  float x0 = 0, x1 = 0;           // visual span inside the line
  int level = 0;                  // odd levels are right-to-left
};

struct EditCaret {
  std::size_t offset = 0; // byte offset in the whole text
  float x = 0;            // visual position inside the line
  int level = 0;
};

struct EditLine {
  std::size_t begin = 0, end = 0; // newline excluded
  float width = 0;
  float origin = 0; // left edge in layout coordinates (right aligned in RTL)
  std::vector<EditCluster> clusters;
  std::vector<EditCaret> carets;
};

struct EditLayout {
  std::vector<EditLine> lines;
  float width = 0;         // widest line
  float height = 0;        // lines * line_height
  float line_height = 0;
  float content_width = 0; // width the lines are aligned inside
  bool rtl = false;
};

struct CaretPosition {
  int line = 0;
  std::size_t offset = 0;
  float x = 0; // layout coordinate: line origin + caret x
  int level = 0;
};

bool shaped_editing_available();
// True when the text field must use the shaped editor rather than ImGui's.
bool text_needs_shaped_editor(const char *text, bool rtl);
// Splits text into paragraphs and wrapped lines, then describes each line's
// clusters and caret stops. `content_width` is the width lines align inside.
void layout_edit_text(const char *text, std::size_t size, int weight, float font_size, bool rtl, bool wrap,
                      float wrap_width, float content_width, EditLayout &out);
// Height of the wrapped text, without padding.
float edit_text_height(const char *text, std::size_t size, int weight, float font_size, bool rtl, bool wrap,
                       float wrap_width);

// Caret at a byte offset. When an offset has several visual positions (a bidi
// run boundary), the one nearest `hint_x` wins; `line_hint` keeps the caret on
// the line the user is moving on.
CaretPosition caret_at(const EditLayout &layout, std::size_t offset, float hint_x = 0,
                       int line_hint = -1);
CaretPosition caret_at_point(const EditLayout &layout, float x, float y);
// Local (inside line) helpers on top of caret_at:
CaretPosition line_edge(const EditLayout &layout, int line, bool left);
// dir = -1 visually left, +1 visually right. Crosses line boundaries.
CaretPosition move_caret(const EditLayout &layout, const CaretPosition &from, int dir);
// dir = -1 previous line, +1 next line (keeps the horizontal position).
CaretPosition move_line(const EditLayout &layout, const CaretPosition &from, int dir);

// Clipboard and selection editing. Offsets stay in bytes and ranges are always
// logical min..max, so an RTL selection copies the text in storage order.
std::string selection_text(const std::string &text, std::size_t cursor, std::size_t anchor);
// Replace the selection with `insert`; the caret lands after the insertion.
void replace_selection(std::string &text, std::size_t &cursor, std::size_t &anchor, const std::string &insert);
// Erase the selection; the caret lands at its start.
void erase_selection(std::string &text, std::size_t &cursor, std::size_t &anchor);
// The byte range Backspace (dir -1) or Delete (dir +1) removes at `caret` when
// no selection is active: the neighbouring character on the visual side,
// including a line break. False when there is nothing to delete.
bool visual_delete_range(const char *text, std::size_t size, const EditLayout &layout,
                         const CaretPosition &caret, int dir, std::size_t &begin, std::size_t &end);
// Clipboard text as it enters a field: CRLF and CR become LF, and single-line
// fields drop line breaks and turn tabs into spaces.
std::string sanitize_paste(const std::string &input, bool multiline);

// In-progress IME composition. The field's stored text is untouched until the
// composition commits; the preview replaces the selection captured when the
// composition began. This is deliberately windowless so hosts and tests can
// drive it without a platform IME.
struct ImeEdit {
  bool active = false;
  std::string preview;
  std::size_t cursor = 0, anchor = 0; // selection at composition start
};

void ime_begin(ImeEdit &ime, const std::string &preview, std::size_t cursor, std::size_t anchor);
void ime_update(ImeEdit &ime, const std::string &preview);
// Replace the captured selection with the preview and move cursor/anchor after
// it. Returns true when the field text changed. An empty preview commits
// nothing, leaving the field and selection as they were.
bool ime_commit(std::string &text, ImeEdit &ime, std::size_t &cursor, std::size_t &anchor);
void ime_cancel(ImeEdit &ime);
// Text to shape and draw while composing: the preview replaces the captured
// selection, so mixed-script previews are ordered in their surrounding text.
std::string ime_preview_text(const std::string &text, const ImeEdit &ime);
// Byte offset of the caret after the preview in that display text.
std::size_t ime_preview_caret(const ImeEdit &ime);

std::size_t next_byte(const char *text, std::size_t size, std::size_t pos);
std::size_t prev_byte(const char *text, std::size_t size, std::size_t pos);
std::string encode_utf8(unsigned int codepoint);

} // namespace silta::detail
