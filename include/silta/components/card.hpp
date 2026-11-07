#pragma once
// shadcn/ui Card and its parts, as scopes:
//
//   if (auto card = silta::Card({.width = 380})) {
//     { auto header = silta::CardHeader();
//       silta::CardTitle("Create project");
//       silta::CardDescription("Deploy your new project in one-click."); }
//     { auto content = silta::CardContent({.gap = 16}); ... }
//     { auto footer = silta::CardFooter({.justify = silta::Justify::between}); ... }
//   }
#include <optional>
#include "silta/layout.hpp"

namespace silta {

struct CardOptions {
  float width = 0;  // points, 0 = fit content, fill = available
  float height = 0;
  const char *id = nullptr;
};

struct SectionOptions {
  float gap = -1; // points; -1 = the section's default (header 6, others 0)
  Justify justify = Justify::start;
  std::optional<Align> align; // default: stretch, CardFooter: center
};

class [[nodiscard]] Card {
public:
  explicit Card(const CardOptions &options = {});
  explicit operator bool() const { return true; }

private:
  Flex flex_;
};

class [[nodiscard]] CardHeader {
public:
  explicit CardHeader(const SectionOptions &options = {});
  explicit operator bool() const { return true; }

private:
  Flex flex_;
};

class [[nodiscard]] CardContent {
public:
  explicit CardContent(const SectionOptions &options = {});
  explicit operator bool() const { return true; }

private:
  Flex flex_;
};

class [[nodiscard]] CardFooter {
public:
  explicit CardFooter(const SectionOptions &options = {});
  explicit operator bool() const { return true; }

private:
  Flex flex_;
};

void CardTitle(const char *text);
void CardDescription(const char *text);

} // namespace silta
