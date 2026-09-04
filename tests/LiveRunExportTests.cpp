#include "../LiveRunExport.h"

#include <cassert>
#include <iostream>
#include <string>

int main()
{
	assert(!LiveRunExport::validateOutputFolder(""));
	assert(!LiveRunExport::validateOutputFolder("sa2026-baseline"));
	assert(!LiveRunExport::validateOutputFolder("sa2026-new-preference-method"));
	assert(!LiveRunExport::validateOutputFolder("sa2026.v2"));

	std::u8string const unicodeFolder = u8"sa2026-\u57FA\u51C6";
	std::string const unicode(
		reinterpret_cast<char const*>(unicodeFolder.data()),
		unicodeFolder.size());
	assert(!LiveRunExport::validateOutputFolder(unicode));

	assert(LiveRunExport::validateOutputFolder(".").has_value());
	assert(LiveRunExport::validateOutputFolder("..").has_value());
	assert(LiveRunExport::validateOutputFolder("...").has_value());
	assert(LiveRunExport::validateOutputFolder("....").has_value());
	assert(LiveRunExport::validateOutputFolder("foo/bar").has_value());
	assert(LiveRunExport::validateOutputFolder("foo\\bar").has_value());
	assert(LiveRunExport::validateOutputFolder("C:\\live").has_value());
	assert(LiveRunExport::validateOutputFolder("/tmp/live").has_value());
	assert(LiveRunExport::validateOutputFolder("C:live").has_value());
	assert(LiveRunExport::validateOutputFolder("live:run").has_value());
	assert(LiveRunExport::validateOutputFolder("live?run").has_value());
	assert(LiveRunExport::validateOutputFolder("live*run").has_value());
	assert(LiveRunExport::validateOutputFolder("live|run").has_value());
	assert(LiveRunExport::validateOutputFolder("live<run").has_value());
	assert(LiveRunExport::validateOutputFolder("live>run").has_value());
	assert(LiveRunExport::validateOutputFolder("live\"run").has_value());
	assert(LiveRunExport::validateOutputFolder(" sa2026").has_value());
	assert(LiveRunExport::validateOutputFolder("sa2026 ").has_value());
	assert(LiveRunExport::validateOutputFolder("sa2026.").has_value());
	assert(LiveRunExport::validateOutputFolder("CON").has_value());
	assert(LiveRunExport::validateOutputFolder("nul.txt").has_value());
	assert(LiveRunExport::validateOutputFolder("COM1").has_value());
	assert(LiveRunExport::validateOutputFolder("lpt9.results").has_value());

	std::cout << "Live run export tests passed\n";
}
