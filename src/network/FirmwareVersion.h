#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace firmware_version {
struct Version {
  std::array<uint32_t, 4> parts{};
  bool rc = false;
};

inline bool number(std::string_view& text, uint32_t& result) {
  if (text.empty() || text.front() < '0' || text.front() > '9') return false;
  result = 0;
  while (!text.empty() && text.front() >= '0' && text.front() <= '9') {
    const unsigned digit = text.front() - '0';
    if (result > (UINT32_MAX - digit) / 10) return false;
    result = result * 10 + digit;
    text.remove_prefix(1);
  }
  return true;
}

// Accept two-part releases (15.04), semver, and historical -ko.N releases.
// Malformed remote tags must not throw or make an update appear newer.
inline bool parse(std::string_view text, Version& version) {
  version = {};
  if (text.starts_with("v")) text.remove_prefix(1);
  if (!number(text, version.parts[0]) || !text.starts_with(".")) return false;
  text.remove_prefix(1);
  if (!number(text, version.parts[1])) return false;
  if (text.starts_with(".")) {
    text.remove_prefix(1);
    if (!number(text, version.parts[2])) return false;
  }
  if (text.starts_with("-ko.")) {
    text.remove_prefix(4);
    if (!number(text, version.parts[3])) return false;
  }
  if (text.starts_with("-rc")) {
    version.rc = true;
    text.remove_prefix(3);
  }
  if (text.starts_with("+") && text.size() > 1) return true;
  return text.empty();
}

inline bool isNewer(std::string_view candidate, std::string_view current) {
  Version next, installed;
  if (!parse(candidate, next) || !parse(current, installed)) return false;
  if (next.parts != installed.parts) return next.parts > installed.parts;
  return installed.rc && !next.rc;
}
}  // namespace firmware_version
