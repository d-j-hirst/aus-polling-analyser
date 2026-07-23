#pragma once

// GCC 9 implements the source-location proposal in its standard-library
// experimental namespace. Newer standard libraries expose the C++20 type.
#if __has_include(<source_location>)
#include <source_location>
using SourceLocation = std::source_location;
#elif __has_include(<experimental/source_location>)
#include <experimental/source_location>
using SourceLocation = std::experimental::source_location;
#else
#error "This compiler does not provide source-location support."
#endif
