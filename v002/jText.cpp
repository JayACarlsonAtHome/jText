#include "jText.h"
#include "parser/line.hpp"
#include "parser/section.hpp"
#include "parser/utf8.hpp"
#include <charconv>
#include <filesystem>
#include <optional>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <unordered_set>
#include <stdexcept>
#include <chrono>
#include <format>

namespace fs = std::filesystem;
using namespace std::literals;

namespace jText::v002 {
#include "jText.impl.inc"
} // namespace jText::v002
