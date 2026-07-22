#pragma once

#include <filesystem>
#include <string>

// Resolves repository-relative data paths without requiring the process to be
// launched from the repository root.
class WorkspacePaths {
public:
	explicit WorkspacePaths(std::filesystem::path root);

	// Searches upwards from a project-file hint and the current directory for
	// the repository's analysis data directory.
	static WorkspacePaths discover(std::filesystem::path const& hint = {});

	std::filesystem::path const& root() const { return rootPath; }
	std::filesystem::path resolve(std::filesystem::path const& path) const;
	std::string resolveString(std::filesystem::path const& path) const;

private:
	static std::filesystem::path findRoot(std::filesystem::path start);

	std::filesystem::path rootPath;
};
