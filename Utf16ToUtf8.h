#pragma once

#include "tinyxml2.h"

#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>

namespace XmlEncoding {

inline std::string utf16ToUtf8(std::u16string const& utf16)
{
	std::string utf8;
	utf8.reserve(utf16.size());

	for (size_t i = 0; i < utf16.size(); ++i) {
		uint32_t codePoint = utf16[i];
		if (codePoint >= 0xD800 && codePoint <= 0xDBFF) {
			if (i + 1 >= utf16.size()) {
				throw std::runtime_error("Invalid UTF-16 data: truncated surrogate pair");
			}
			uint32_t low = utf16[i + 1];
			if (low < 0xDC00 || low > 0xDFFF) {
				throw std::runtime_error("Invalid UTF-16 data: malformed surrogate pair");
			}
			codePoint = 0x10000 + (((codePoint - 0xD800) << 10) | (low - 0xDC00));
			++i;
		}
		else if (codePoint >= 0xDC00 && codePoint <= 0xDFFF) {
			throw std::runtime_error("Invalid UTF-16 data: unexpected low surrogate");
		}

		if (codePoint <= 0x7F) {
			utf8.push_back(static_cast<char>(codePoint));
		}
		else if (codePoint <= 0x7FF) {
			utf8.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
			utf8.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
		}
		else if (codePoint <= 0xFFFF) {
			utf8.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
			utf8.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
			utf8.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
		}
		else {
			utf8.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
			utf8.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
			utf8.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
			utf8.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
		}
	}

	return utf8;
}

inline void loadUtf16AwareXmlDocument(tinyxml2::XMLDocument& document, std::string const& filename)
{
	auto loadResult = document.LoadFile(filename.c_str());
	if (loadResult == tinyxml2::XML_SUCCESS) return;
	char const* initialError = document.ErrorStr();

	std::ifstream input(filename, std::ios::binary);
	if (!input) {
		throw std::runtime_error("Failed to open XML file: " + filename);
	}

	std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
	if (bytes.size() < 2) {
		throw std::runtime_error(
			"Failed to load XML file: " + filename + " (" +
			(initialError ? initialError : "unknown TinyXML2 error") + ")"
		);
	}

	bool const isUtf16Le = static_cast<unsigned char>(bytes[0]) == 0xFF &&
		static_cast<unsigned char>(bytes[1]) == 0xFE;
	bool const isUtf16Be = static_cast<unsigned char>(bytes[0]) == 0xFE &&
		static_cast<unsigned char>(bytes[1]) == 0xFF;
	if (!isUtf16Le && !isUtf16Be) {
		throw std::runtime_error(
			"Failed to load XML file: " + filename + " (" +
			(initialError ? initialError : "unknown TinyXML2 error") + ")"
		);
	}

	std::u16string utf16;
	utf16.reserve((bytes.size() - 2) / 2);
	for (size_t i = 2; i + 1 < bytes.size(); i += 2) {
		unsigned char first = static_cast<unsigned char>(bytes[i]);
		unsigned char second = static_cast<unsigned char>(bytes[i + 1]);
		char16_t codeUnit = isUtf16Le
			? static_cast<char16_t>(first | (second << 8))
			: static_cast<char16_t>((first << 8) | second);
		utf16.push_back(codeUnit);
	}

	std::string utf8 = utf16ToUtf8(utf16);

	document.Clear();
	loadResult = document.Parse(utf8.c_str(), utf8.size());
	if (loadResult != tinyxml2::XML_SUCCESS) {
		throw std::runtime_error("Failed to parse UTF-16 XML file after transcoding: " + filename);
	}
}

}
