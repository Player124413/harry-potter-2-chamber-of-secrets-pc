#pragma once

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <system_error>
#include <vector>
#include <cstdint>
#include <cstdio>

#if defined(_WIN32)
#include <cwchar>
#include <process.h>
#include <windows.h>
#include <io.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#endif

namespace HP2TestProcess
{
namespace detail
{
inline std::string RandomHex()
{
	static const char digits[] = "0123456789abcdef";
	std::random_device random;
	std::string value;
	value.reserve(32);
	for (int i = 0; i < 32; ++i)
		value.push_back(digits[random() & 0xf]);
	return value;
}


#if defined(_WIN32)
inline std::wstring Wide(const std::string& text)
{
	if (text.empty()) return {};
	const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
		static_cast<int>(text.size()), nullptr, 0);
	if (!size) return {};
	std::wstring result(static_cast<size_t>(size), L'\0');
	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
		static_cast<int>(text.size()), result.data(), size);
	return result;
}
#else
inline std::string SelfPath()
{
#if defined(__APPLE__)
	uint32_t size = 0;
	_NSGetExecutablePath(nullptr, &size);
	std::string path(size, '\0');
	if (_NSGetExecutablePath(&path[0], &size) != 0) return {};
	path.resize(std::char_traits<char>::length(path.c_str()));
	return path;
#else
	char path[4096];
	const ssize_t size = readlink("/proc/self/exe", path, sizeof(path) - 1);
	if (size <= 0) return {};
	path[size] = '\0';
	return path;
#endif
}
#endif
}

// args excludes argv[0]. env contains KEY=VALUE overrides; other current
// process environment entries are inherited.
inline int RunSelf(const std::vector<std::string>& args,
	const std::vector<std::string>& env, std::string& stdoutText)
{
	stdoutText.clear();
#if defined(_WIN32)
	wchar_t executable[32768];
	const DWORD executableLength = GetModuleFileNameW(nullptr, executable, 32768);
	if (!executableLength || executableLength >= 32768) return -1;
	std::vector<std::wstring> wideArgs;
	wideArgs.emplace_back(executable, executableLength);
	for (const auto& arg : args) wideArgs.push_back(detail::Wide(arg));
	std::vector<const wchar_t*> argv;
	for (const auto& arg : wideArgs) argv.push_back(arg.c_str());
	argv.push_back(nullptr);

	std::vector<std::wstring> environment;
	LPWCH block = GetEnvironmentStringsW();
	if (!block) return -1;
	for (const wchar_t* entry = block; *entry; entry += wcslen(entry) + 1)
		environment.emplace_back(entry);
	FreeEnvironmentStringsW(block);
	for (const auto& assignment : env)
	{
		const size_t equal = assignment.find('=');
		if (equal == std::string::npos) continue;
		const std::wstring key = detail::Wide(assignment.substr(0, equal));
		const std::wstring replacement = detail::Wide(assignment);
		bool replaced = false;
		for (auto& item : environment)
		{
			const size_t itemEqual = item.find(L'=');
			if (itemEqual == key.size() && _wcsnicmp(item.c_str(), key.c_str(), key.size()) == 0)
			{
				item = replacement;
				replaced = true;
				break;
			}
		}
		if (!replaced) environment.push_back(replacement);
	}
	std::vector<const wchar_t*> envp;
	for (const auto& item : environment) envp.push_back(item.c_str());
	envp.push_back(nullptr);
	const auto outputPath = std::filesystem::temp_directory_path()
		/ ("hp2-test-process-" + detail::RandomHex() + ".out");
	const int savedOut = _dup(_fileno(stdout));
	const int savedErr = _dup(_fileno(stderr));
	if (savedOut < 0 || savedErr < 0) return -1;
	int result = -1;
	if (_wfreopen(outputPath.wstring().c_str(), L"wb", stdout)
		&& _dup2(_fileno(stdout), _fileno(stderr)) == 0)
		result = _wspawnve(_P_WAIT, executable, argv.data(), envp.data());
	fflush(stdout);
	_dup2(savedOut, _fileno(stdout));
	_dup2(savedErr, _fileno(stderr));
	_close(savedOut);
	_close(savedErr);
	std::ifstream input(outputPath, std::ios::binary);
	stdoutText.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
	std::error_code ignored;
	std::filesystem::remove(outputPath, ignored);
	return result;
#else
	const std::string executable = detail::SelfPath();
	if (executable.empty()) return -1;
	int pipefd[2];
	if (pipe(pipefd) != 0) return -1;
	std::vector<std::string> storage{executable};
	storage.insert(storage.end(), args.begin(), args.end());
	std::vector<char*> argv;
	for (auto& item : storage) argv.push_back(item.data());
	argv.push_back(nullptr);
	std::vector<std::string> envStorage;
	for (char** item = environ; item && *item; ++item) envStorage.emplace_back(*item);
	for (const auto& assignment : env)
	{
		const size_t equal = assignment.find('=');
		if (equal == std::string::npos) continue;
		const std::string key = assignment.substr(0, equal);
		bool replaced = false;
		for (auto& item : envStorage)
			if (item.compare(0, key.size(), key) == 0 && item.size() > key.size() && item[key.size()] == '=')
			{
				item = assignment;
				replaced = true;
				break;
			}
		if (!replaced) envStorage.push_back(assignment);
	}
	std::vector<char*> envp;
	for (auto& item : envStorage) envp.push_back(item.data());
	envp.push_back(nullptr);
	posix_spawn_file_actions_t actions;
	if (posix_spawn_file_actions_init(&actions) != 0) { close(pipefd[0]); close(pipefd[1]); return -1; }
	posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDOUT_FILENO);
	posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDERR_FILENO);
	posix_spawn_file_actions_addclose(&actions, pipefd[0]);
	posix_spawn_file_actions_addclose(&actions, pipefd[1]);
	pid_t child = -1;
	const int spawned = posix_spawn(&child, executable.c_str(), &actions, nullptr, argv.data(), envp.data());
	posix_spawn_file_actions_destroy(&actions);
	close(pipefd[1]);
	if (spawned != 0) { close(pipefd[0]); return -1; }
	char buffer[4096];
	ssize_t count;
	while ((count = read(pipefd[0], buffer, sizeof(buffer))) > 0)
		stdoutText.append(buffer, static_cast<size_t>(count));
	close(pipefd[0]);
	int status = 0;
	if (waitpid(child, &status, 0) != child || !WIFEXITED(status)) return -1;
	return WEXITSTATUS(status);
#endif
}
}
