// Links the installed application shell; not run (it would open a window).
#include "silta/app.hpp"
#include "silta/silta.hpp"

int main(int argc, char **) {
  if (argc < 0) { // never true: referenced so the linker resolves the shell
    silta::App app;
    return app.run([] { silta::Button("Hello"); });
  }
  return 0;
}
