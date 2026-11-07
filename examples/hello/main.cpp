// The smallest Silta app: one card with a form.
#include "silta/app.hpp"
#include "silta/silta.hpp"
#include <string>
using namespace silta;

int main() {
  App app({.title = "Hello"});
  std::string name;
  return app.run([&] {
    auto page = Column({.gap = 16, .padding = Edges::all(32)});
    if (auto card = Card({.width = 350})) {
      { auto h = CardHeader();
        CardTitle("Create project");
        CardDescription("Deploy your new project in one-click."); }
      { auto c = CardContent();
        Input("name", &name, {.placeholder = "Name of your project"}); }
      { auto f = CardFooter({.justify = Justify::between});
        Button("Cancel", {.variant = Variant::outline});
        if (Button("Deploy")) { /* ... */ } }
    }
  });
}
