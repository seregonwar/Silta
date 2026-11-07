// Silta gallery: every component in every variant, with live theme and
// renderer switching. Run with --backend=vulkan or --backend=d3d11.
#include "silta/app.hpp"
#include "silta/silta.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace silta;

namespace {
struct State {
  bool dark = false;
  BaseColor base = BaseColor::neutral;
  bool airplane = false;
  bool notifications = true;
  bool marketing = false;
  std::string email;
  std::string password;
  std::string project_name;
  std::string framework = "Next.js";
  int clicks = 0;
  long frames = 0;
  // Overlays and controls
  int fruit = -1;
  bool show_status = true, show_activity = false;
  bool terms = true, newsletter = false;
  float volume = 66, brightness = 0.4f;
  int tab = 0, tool = 0;
  bool bold = false, italic = true;
  float progress = 0.25f;
  std::string profile_name = "Pedro Duarte", profile_user = "@peduarte";
  float scroll = 0; // --scroll=N (points), for screenshots
  Backend switch_to = Backend::automatic; // --switch-to=..., tests runtime switching
  bool hidden = false;                     // --hidden
  // --script="click:978,56;type:hi;key:tab;wait:10;shot:a.png;quit"
  std::vector<std::string> script;
  size_t step = 0;
  int wait = 0;
  double sleep_until = 0;
};

// Plays the test script by feeding ImGui the same events a user would make.
// Coordinates are in points from the window's top-left corner.
void run_script(State &s, App &app) {
  if (s.step >= s.script.size()) return;
  request_frames(2);
  if (s.wait > 0) {
    --s.wait;
    return;
  }
  if (ImGui::GetTime() < s.sleep_until) return;
  ImGuiIO &io = ImGui::GetIO();
  const std::string &cmd = s.script[s.step++];
  const size_t colon = cmd.find(':');
  const std::string verb = cmd.substr(0, colon), arg = colon == std::string::npos ? "" : cmd.substr(colon + 1);
  auto point = [&] {
    const size_t comma = arg.find(',');
    return ImVec2(std::stof(arg.substr(0, comma)) * scale(), std::stof(arg.substr(comma + 1)) * scale());
  };
  s.wait = 3; // let ImGui consume the events (they trickle one kind per frame)
  if (verb == "move") {
    io.AddMousePosEvent(point().x, point().y);
  } else if (verb == "click") {
    io.AddMousePosEvent(point().x, point().y);
    io.AddMouseButtonEvent(0, true);
    io.AddMouseButtonEvent(0, false);
  } else if (verb == "type") {
    io.AddInputCharactersUTF8(arg.c_str());
  } else if (verb == "key") {
    const ImGuiKey key = arg == "tab" ? ImGuiKey_Tab : arg == "escape" ? ImGuiKey_Escape
                       : arg == "enter" ? ImGuiKey_Enter : arg == "space" ? ImGuiKey_Space : ImGuiKey_None;
    io.AddKeyEvent(key, true);
    io.AddKeyEvent(key, false);
  } else if (verb == "wait") {
    s.wait = std::stoi(arg);
  } else if (verb == "sleep") { // real time, so animations can finish
    s.sleep_until = ImGui::GetTime() + std::stof(arg) / 1000.0;
  } else if (verb == "shot") {
    app.screenshot(arg.c_str());
  } else if (verb == "quit") {
    app.close();
  }
}

void apply_theme(const State &s) { set_theme(s.dark ? Theme::dark(s.base) : Theme::light(s.base)); }

// A titled block: heading and description, then the examples 16pt below.
class [[nodiscard]] Section {
public:
  Section(const char *title, const char *description) : block_(FlexOptions{.gap = 16, .align = Align::stretch}) {
    auto col = Column({.gap = 4});
    Text(title, {.size = TextSize::xl, .weight = Weight::semibold});
    Text(description, {.muted = true});
  }

private:
  Flex block_;
};

void header(State &s, App &app) {
  auto bar = Row({.gap = 12, .align = Align::center, .justify = Justify::between, .width = fill});
  {
    auto brand = Row({.gap = 10, .align = Align::center});
    Text("Silta", {.size = TextSize::xl2, .weight = Weight::semibold, .line_height = 32});
    Badge("v0.1", {.variant = Variant::secondary});
  }
  {
    auto tools = Row({.gap = 8, .align = Align::center});
    for (Backend b : available_backends()) {
      const bool active = app.backend() == b;
      if (Button(backend_name(b), {.variant = active ? Variant::secondary : Variant::ghost, .size = Size::sm}))
        app.set_backend(b);
    }
    Separator({.vertical = true});
    if (Switch("Dark", &s.dark)) apply_theme(s);
  }
}

void buttons(State &s) {
  auto section = Section("Button", "Displays a button or a component that looks like a button.");
  {
    auto row = Row({.gap = 8, .align = Align::center});
    if (Button("Button")) ++s.clicks;
    Button("Secondary", {.variant = Variant::secondary});
    Button("Destructive", {.variant = Variant::destructive});
    Button("Outline", {.variant = Variant::outline});
    Button("Ghost", {.variant = Variant::ghost});
    Button("Link", {.variant = Variant::link});
  }
  {
    auto row = Row({.gap = 8, .align = Align::center});
    Button("Small", {.variant = Variant::outline, .size = Size::sm});
    Button("Default", {.variant = Variant::outline});
    Button("Large", {.variant = Variant::outline, .size = Size::lg});
    Button("##add", {.variant = Variant::outline, .size = Size::icon, .icon = Icon::plus});
    Button("Login with Email", {.icon = Icon::mail});
    Button("Continue", {.variant = Variant::secondary, .trailing_icon = Icon::chevron_right});
    Button("Please wait", {.loading = true});
    Button("Disabled", {.disabled = true});
  }
  char clicked[64];
  std::snprintf(clicked, sizeof clicked, "The default button was clicked %d times.", s.clicks);
  Text(clicked, {.muted = true});
}

void badges() {
  auto section = Section("Badge", "Displays a badge or a component that looks like a badge.");
  auto row = Row({.gap = 8, .align = Align::center});
  Badge("Badge");
  Badge("Secondary", {.variant = Variant::secondary});
  Badge("Destructive", {.variant = Variant::destructive});
  Badge("Outline", {.variant = Variant::outline});
  Badge("Verified", {.variant = Variant::secondary, .icon = Icon::check});
}

void switches(State &s) {
  auto section = Section("Switch", "A control that allows the user to toggle between checked and not checked.");
  auto row = Row({.gap = 24, .align = Align::center});
  Switch("Airplane Mode", &s.airplane);
  Switch("Notifications", &s.notifications);
  bool locked = true;
  Switch("Locked", &locked, {.disabled = true});
}

void inputs(State &s) {
  auto section = Section("Input", "Displays a form input field or a component that looks like an input field.");
  auto col = Column({.gap = 12, .width = 384});
  Input("email", &s.email, {.placeholder = "Email"});
  Input("password", &s.password, {.placeholder = "Password", .password = true});
  Input("disabled", &s.framework, {.disabled = true});
}

void cards(State &s) {
  auto section = Section("Card", "Displays a card with header, content, and footer.");
  auto row = Row({.gap = 24, .align = Align::start});

  if (auto card = Card({.width = 350})) {
    {
      auto h = CardHeader();
      CardTitle("Create project");
      CardDescription("Deploy your new project in one-click.");
    }
    {
      auto c = CardContent({.gap = 16});
      {
        auto field = Column({.gap = 6});
        Label("Name");
        Input("name", &s.project_name, {.placeholder = "Name of your project"});
      }
      {
        auto field = Column({.gap = 6});
        Label("Framework");
        Input("framework", &s.framework);
      }
    }
    {
      auto f = CardFooter({.justify = Justify::between});
      Button("Cancel", {.variant = Variant::outline});
      Button("Deploy");
    }
  }

  if (auto card = Card({.width = 350})) {
    {
      auto h = CardHeader();
      CardTitle("Notifications");
      CardDescription("You have 3 unread messages.");
    }
    {
      auto c = CardContent({.gap = 16});
      {
        auto box = Row({.gap = 16, .align = Align::center});
        {
          auto icon_box = Column({.width = 24});
          Button("##bell", {.variant = Variant::ghost, .size = Size::icon, .icon = Icon::bell});
        }
        {
          auto text = Column({.gap = 4, .width = fill});
          Text("Push Notifications", {.weight = Weight::medium, .line_height = 14});
          Text("Send notifications to device.", {.muted = true});
        }
        Switch("##push", &s.notifications);
      }
      Separator();
      {
        auto box = Row({.gap = 16, .align = Align::center});
        {
          auto text = Column({.gap = 4, .width = fill});
          Text("Marketing emails", {.weight = Weight::medium, .line_height = 14});
          Text("Receive emails about new products.", {.muted = true});
        }
        Switch("##marketing", &s.marketing);
      }
    }
    {
      auto f = CardFooter();
      Button("Mark all as read", {.icon = Icon::check, .width = fill});
    }
  }
}

void palette(State &s) {
  auto section = Section("Theme", "shadcn/ui base colors, converted from their OKLCH tokens.");
  auto row = Row({.gap = 8, .align = Align::center});
  struct Option {
    const char *name;
    BaseColor base;
  };
  const Option options[] = {{"Neutral", BaseColor::neutral}, {"Zinc", BaseColor::zinc}, {"Slate", BaseColor::slate},
                            {"Stone", BaseColor::stone},     {"Gray", BaseColor::gray}};
  for (const Option &o : options) {
    if (Button(o.name, {.variant = s.base == o.base ? Variant::primary : Variant::outline, .size = Size::sm})) {
      s.base = o.base;
      apply_theme(s);
    }
  }
}


void overlays(State &s) {
  auto section = Section("Overlays", "Popover, Dropdown Menu, Select, Tooltip and Dialog.");
  auto row = Row({.gap = 8, .align = Align::center});

  if (Button("Open popover", {.variant = Variant::outline})) OpenPopover("dimensions");
  if (auto pop = Popover("dimensions", {.width = 320, .gap = 16})) {
    {
      auto head = Column({.gap = 6});
      Text("Dimensions", {.weight = Weight::medium, .line_height = 14});
      Text("Set the dimensions for the layer.", {.wrap = true, .muted = true});
    }
    static std::string width = "100%", height = "25px";
    for (auto [label, value] : {std::pair{"Width", &width}, std::pair{"Height", &height}}) {
      auto field = Row({.gap = 16, .align = Align::center});
      { auto l = Column({.width = 70}); Label(label); }
      Input(label, value, {.width = fill});
    }
  }

  if (Button("Menu", {.variant = Variant::outline, .trailing_icon = Icon::chevron_down})) OpenPopover("menu");
  if (auto menu = DropdownMenu("menu", {.min_width = 224})) {
    MenuLabel("My Account");
    MenuSeparator();
    MenuItem("Profile", {.icon = Icon::user, .shortcut = "Shift+Ctrl+P"});
    MenuItem("Settings", {.icon = Icon::settings, .shortcut = "Ctrl+S"});
    MenuItem("Notifications", {.icon = Icon::bell});
    MenuSeparator();
    MenuCheckbox("Status Bar", &s.show_status);
    MenuCheckbox("Activity Bar", &s.show_activity);
    MenuSeparator();
    if (MenuItem("Delete project", {.icon = Icon::trash, .destructive = true}))
      toast("Project deleted", {.kind = ToastKind::error});
    MenuItem("API (disabled)", {.disabled = true});
  }

  static const char *const fruits[] = {"Apple", "Banana", "Blueberry", "Grapes", "Pineapple"};
  if (Select("fruit", &s.fruit, fruits, {.placeholder = "Select a fruit", .width = 180}))
    toast("Fruit selected", {.description = fruits[s.fruit]});

  Button("##info", {.variant = Variant::outline, .size = Size::icon, .icon = Icon::info});
  Tooltip("Add to library");

  if (Button("Edit profile", {.variant = Variant::outline})) OpenDialog("profile");
  if (auto dlg = Dialog("profile", {.title = "Edit profile",
                                    .description = "Make changes to your profile here. Click save when you're done.",
                                    .width = 425})) {
    {
      auto fields = Column({.gap = 16});
      for (auto [label, value] : {std::pair{"Name", &s.profile_name}, std::pair{"Username", &s.profile_user}}) {
        auto field = Column({.gap = 8});
        Label(label);
        Input(label, value);
      }
    }
    auto footer = DialogFooter();
    if (Button("Cancel", {.variant = Variant::outline})) CloseDialog();
    if (Button("Save changes")) {
      CloseDialog();
      toast("Profile updated", {.description = s.profile_name.c_str(), .kind = ToastKind::success});
    }
  }

  if (Button("Show toast", {.variant = Variant::secondary}))
    toast("Event has been created", {.description = "Sunday, December 03, 2023 at 9:00 AM"});
}

void controls(State &s) {
  auto section = Section("Controls", "Checkbox, Slider, Tabs, Toggle, Toggle Group, Progress and Kbd.");
  {
    auto row = Row({.gap = 24, .align = Align::center});
    Checkbox("Accept terms and conditions", &s.terms);
    Checkbox("Newsletter", &s.newsletter);
    static bool off = false;
    Checkbox("Disabled", &off, {.disabled = true});
  }
  {
    auto row = Row({.gap = 24, .align = Align::center, .width = fill});
    { auto c = Column({.width = 300}); Slider("volume", &s.volume, 0, 100, {.step = 1}); }
    char v[32];
    std::snprintf(v, sizeof v, "Volume %.0f", s.volume);
    Text(v, {.muted = true});
    { auto c = Column({.width = 200}); Slider("brightness", &s.brightness, 0, 1); }
  }
  {
    auto row = Row({.gap = 24, .align = Align::center});
    static const char *const tabs[] = {"Account", "Password", "Notifications"};
    Tabs("tabs", &s.tab, tabs);
    static const char *const views[] = {"Day", "Week", "Month"};
    static int view = 1;
    { auto c = Column({.width = 260}); Tabs("views", &view, views, {.width = fill}); }
  }
  {
    auto row = Row({.gap = 12, .align = Align::center});
    Toggle("##bold", &s.bold, {.icon = Icon::type});
    Toggle("Italic", &s.italic, {.outline = true});
    static const ToggleGroupItem tools[] = {{nullptr, Icon::pointer, "Select (V)"},
                                            {nullptr, Icon::scissors, "Razor (C)"},
                                            {nullptr, Icon::magnet, "Snapping (N)"}};
    ToggleGroup("tools", &s.tool, tools, {.outline = true});
    static const ToggleGroupItem sizes[] = {{"S"}, {"M"}, {"L"}};
    static int size = 1;
    ToggleGroup("sizes", &size, sizes, {.size = Size::sm});
  }
  {
    auto row = Row({.gap = 16, .align = Align::center, .width = fill});
    { auto c = Column({.width = 300}); Progress("progress", s.progress); }
    if (Button("+10%", {.variant = Variant::outline, .size = Size::sm})) s.progress = std::min(1.f, s.progress + 0.1f);
    if (Button("Reset", {.variant = Variant::ghost, .size = Size::sm})) s.progress = 0;
    Text("Press", {.muted = true});
    Kbd("Ctrl");
    Kbd("K");
    Text("to search", {.muted = true});
  }
}

void icons() {
  auto section = Section("Icons", "Lucide-style vector icons, crisp at any scale.");
  static const Icon all[] = {Icon::check, Icon::x, Icon::plus, Icon::minus, Icon::chevron_down,
                             Icon::chevrons_up_down, Icon::arrow_right, Icon::more_horizontal, Icon::grip_vertical,
                             Icon::search, Icon::settings, Icon::user, Icon::bell, Icon::info, Icon::alert, Icon::sun,
                             Icon::moon, Icon::maximize, Icon::copy, Icon::eye, Icon::eye_off, Icon::lock,
                             Icon::unlock, Icon::mail, Icon::folder_open, Icon::upload, Icon::download, Icon::film,
                             Icon::image, Icon::music, Icon::type, Icon::layers, Icon::play, Icon::pause,
                             Icon::skip_back, Icon::skip_forward, Icon::volume, Icon::volume_off, Icon::scissors,
                             Icon::trash, Icon::undo, Icon::redo, Icon::zoom_in, Icon::zoom_out, Icon::magnet,
                             Icon::pointer};
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const size_t per_row = 23;
  for (size_t start = 0; start < std::size(all); start += per_row) {
    auto row = Row({.gap = 20});
    for (size_t i = start; i < std::min(std::size(all), start + per_row); ++i) {
      const ImRect r = layout::allocate(ImVec2(px(20), px(20)));
      draw_icon(dl, all[i], r.Min, px(20), theme().foreground);
    }
  }
}

void panels() {
  auto section = Section("Resizable and Scroll Area", "Drag the divider; the list scrolls inside its pane.");
  auto frame = Column({.width = fill, .height = 220});
  const ImRect r = frame.rect();
  if (r.GetWidth() > 0) draw::border(ImGui::GetWindowDrawList(), r, theme().border, px(theme().radius_lg()), px(1));
  auto split = Split("gallery-split", {.initial = 0.35f});
  {
    auto left = split.first();
    auto scroll = ScrollArea("tags", {.padding = Edges::all(16), .gap = 8});
    Text("Tags", {.weight = Weight::medium});
    for (int i = 1; i <= 30; ++i) {
      char tag[32];
      std::snprintf(tag, sizeof tag, "v1.2.0-beta.%d", 31 - i);
      Text(tag);
      Separator();
    }
  }
  {
    auto right = split.second({.padding = Edges::all(24), .align = Align::center, .justify = Justify::center});
    Text("Content", {.size = TextSize::lg, .weight = Weight::semibold});
  }
}

// --backend=vulkan|d3d11  --dark  --base=zinc|slate|stone|gray  --scroll=N
// --switch-to=vulkan|d3d11 (swaps the renderer at frame 3)  --hidden  --script=...
Backend parse_args(int argc, char **argv, State &s) {
  Backend backend = Backend::automatic;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--backend=vulkan") backend = Backend::vulkan;
    else if (a == "--backend=d3d11") backend = Backend::d3d11;
    else if (a == "--dark") s.dark = true;
    else if (a == "--base=zinc") s.base = BaseColor::zinc;
    else if (a == "--base=slate") s.base = BaseColor::slate;
    else if (a == "--base=stone") s.base = BaseColor::stone;
    else if (a == "--base=gray") s.base = BaseColor::gray;
    else if (a.rfind("--scroll=", 0) == 0) s.scroll = std::stof(a.substr(9));
    else if (a == "--switch-to=vulkan") s.switch_to = Backend::vulkan;
    else if (a == "--switch-to=d3d11") s.switch_to = Backend::d3d11;
    else if (a == "--hidden") s.hidden = true;
    else if (a.rfind("--script=", 0) == 0) {
      std::string rest = a.substr(9);
      for (size_t pos; (pos = rest.find(';')) != std::string::npos; rest.erase(0, pos + 1))
        s.script.push_back(rest.substr(0, pos));
      if (!rest.empty()) s.script.push_back(rest);
    }
  }
  return backend;
}
} // namespace

int main(int argc, char **argv) {
  State s;
  const Backend backend = parse_args(argc, argv, s);
  App app({.title = "Silta Gallery", .width = 1180, .height = 900, .backend = backend, .hidden = s.hidden});
  if (!app.ok()) return 1;
  apply_theme(s);

  return app.run([&] {
    ++s.frames;
    if (s.scroll > 0 && s.frames < 4) ImGui::SetScrollY(s.scroll * scale());
    if (s.switch_to != Backend::automatic && s.frames == 3) app.set_backend(s.switch_to);
    if (s.step < s.script.size()) request_frames(2); // keep the loop awake while scripted
    if (s.frames > 3) run_script(s, app);
    // Centered page, max-w-5xl with responsive side padding.
    const float pad = layout::at_least(layout::Breakpoint::md) ? 40.f : 16.f;
    auto page = Row({.padding = Edges::all(pad), .justify = Justify::center, .width = fill});
    const float avail = ImGui::GetContentRegionAvail().x / scale() - 2 * pad;
    auto content = Column({.gap = 40, .align = Align::stretch, .width = std::min(1024.f, std::max(320.f, avail))});

    header(s, app);
    Separator();
    palette(s);
    buttons(s);
    badges();
    switches(s);
    inputs(s);
    cards(s);
    overlays(s);
    controls(s);
    panels();
    icons();
    Separator();
    char status[128];
    std::snprintf(status, sizeof status, "Renderer: %s  ·  Frames rendered: %ld (rendering on demand)",
                  backend_name(app.backend()), s.frames);
    Text(status, {.size = TextSize::xs, .muted = true});
    Toaster();
  });
}
