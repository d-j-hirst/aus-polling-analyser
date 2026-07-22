#include "WorkspacePaths.h"

#include <system_error>
#include <utility>

namespace {
	std::filesystem::path absolutePath(std::filesystem::path const& path)
	{
		std::error_code error;
		auto absolute = std::filesystem::absolute(path, error);
		return error ? path : absolute.lexically_normal();
	}

	bool isWorkspaceRoot(std::filesystem::path const& path)
	{
		std::error_code error;
		return std::filesystem::is_directory(
			path / "analysis" / "Data", error);
	}
}

WorkspacePaths::WorkspacePaths(std::filesystem::path root)
	: rootPath(absolutePath(std::move(root)))
{
}

WorkspacePaths WorkspacePaths::discover(std::filesystem::path const& hint)
{
	if (!hint.empty()) {
		auto hintPath = absolutePath(hint);
		std::error_code error;
		if (!std::filesystem::is_directory(hintPath, error)) {
			hintPath = hintPath.parent_path();
		}
		if (auto root = findRoot(std::move(hintPath)); !root.empty()) {
			return WorkspacePaths(std::move(root));
		}
	}

	std::error_code error;
	auto currentPath = std::filesystem::current_path(error);
	if (!error) {
		if (auto root = findRoot(currentPath); !root.empty()) {
			return WorkspacePaths(std::move(root));
		}
		// Preserve the previous working-directory behaviour when no repository
		// marker is available, including during creation of a new workspace.
		return WorkspacePaths(std::move(currentPath));
	}

	return WorkspacePaths(".");
}

std::filesystem::path WorkspacePaths::resolve(
	std::filesystem::path const& path) const
{
	if (path.is_absolute()) return path.lexically_normal();
	return (rootPath / path).lexically_normal();
}

std::string WorkspacePaths::resolveString(
	std::filesystem::path const& path) const
{
	return resolve(path).string();
}

std::filesystem::path WorkspacePaths::findRoot(std::filesystem::path start)
{
	start = absolutePath(std::move(start));
	while (!start.empty()) {
		if (isWorkspaceRoot(start)) return start;
		auto const parent = start.parent_path();
		if (parent == start) break;
		start = parent;
	}
	return {};
}
