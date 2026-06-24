#include "jText.h"
#include <charconv>
#include <filesystem>
#include <optional>
#include <sstream>
#include <algorithm>
#include <unordered_set>
#include <stdexcept>
#include <chrono>
#include <format>

namespace fs = std::filesystem;
using namespace std::literals;

namespace jText::v002 {
#include "jText.impl.inc"
} // namespace jText::v002
