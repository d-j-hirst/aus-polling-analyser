#include "../WorkspacePaths.h"

#include <cassert>
#include <filesystem>
#include <iostream>

int main(int argc, char const* argv[])
{
	auto const explicitRoot = std::filesystem::absolute("workspace-root");
	WorkspacePaths const explicitPaths(explicitRoot);
	assert(explicitPaths.resolve("analysis/data.csv") ==
		(explicitRoot / "analysis/data.csv").lexically_normal());

	auto const absoluteFile = std::filesystem::absolute("external/data.csv");
	assert(explicitPaths.resolve(absoluteFile) == absoluteFile.lexically_normal());

	// The project-file hint may point below the root and need not itself exist.
	auto const workspaceRoot = argc > 1
		? std::filesystem::path(argv[1])
		: std::filesystem::current_path();
	auto const discovered = WorkspacePaths::discover(
		workspaceRoot / "tests/example.pol2");
	auto const expectedDataDirectory =
		(workspaceRoot / "analysis/Data").lexically_normal();
	assert(discovered.resolve("analysis/Data") == expectedDataDirectory);

	std::cout << "Workspace path tests passed\n";
}
