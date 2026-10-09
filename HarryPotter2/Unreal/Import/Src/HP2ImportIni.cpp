#include <cerrno>
#include <cstring>
#include <tuple>
#include "HP2ImportInternal.h"

namespace HP2Import::Detail {
using Lines = std::vector<std::string>;
namespace {
std::string Read(const fs::path& path) {
    errno = 0;
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        const int code = errno;
        Fail("cannot read config " + Utf8(path) + ": [Errno " + std::to_string(code) + "] " + std::strerror(code) + ": '" + Utf8(path) + "'");
    }
    std::string data((std::istreambuf_iterator<char>(file)), {});
    if (file.bad()) Fail("cannot read config " + Utf8(path) + ": read error");
    return data;
}
void ValidateUtf8(const std::string& data, const fs::path& path) {
    for (std::size_t i = 0; i < data.size();) {
        const auto c = static_cast<unsigned char>(data[i]);
        if (c < 0x80) { ++i; continue; }
        const std::size_t n = c >= 0xc2 && c <= 0xdf ? 2 : c >= 0xe0 && c <= 0xef ? 3 : c >= 0xf0 && c <= 0xf4 ? 4 : 0;
        bool valid = n != 0 && i + n <= data.size();
        bool invalidContinuation = false;
        for (std::size_t j = 1; valid && j < n; ++j) {
            if ((static_cast<unsigned char>(data[i + j]) & 0xc0) != 0x80) { valid = false; invalidContinuation = true; }
        }
        if (valid && n == 3) {
            const auto d = static_cast<unsigned char>(data[i + 1]);
            valid = !((c == 0xe0 && d < 0xa0) || (c == 0xed && d >= 0xa0));
        }
        if (valid && n == 4) {
            const auto d = static_cast<unsigned char>(data[i + 1]);
            valid = !((c == 0xf0 && d < 0x90) || (c == 0xf4 && d >= 0x90));
        }
        if (!valid) {
            std::string reason;
            if (n == 0) reason = "invalid start byte";
            else if (i + n > data.size()) reason = "unexpected end of data";
            else if ((c == 0xe0 && i + 1 < data.size() && static_cast<unsigned char>(data[i + 1]) < 0xa0) ||
                     (c == 0xed && i + 1 < data.size() && static_cast<unsigned char>(data[i + 1]) >= 0xa0) ||
                     (c == 0xf0 && i + 1 < data.size() && static_cast<unsigned char>(data[i + 1]) < 0x90) ||
                     (c == 0xf4 && i + 1 < data.size() && static_cast<unsigned char>(data[i + 1]) >= 0x90)) reason = "invalid continuation byte";
            else if (invalidContinuation) reason = "invalid continuation byte";
            else reason = "invalid start byte";
            std::ostringstream message;
            message << "config is not UTF-8/ASCII " << Utf8(path) << ": 'utf-8' codec can't decode byte 0x"
                    << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned int>(c)
                    << std::dec << " in position " << i << ": " << reason;
            Fail(message.str());
        }
        i += n;
    }
}
bool Starts(const std::string& value, const std::string& prefix) { return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0; }
std::string Trim(std::string s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}
std::pair<std::size_t, std::size_t> Span(const Lines& lines, const std::string& section) {
    std::size_t start = lines.size();
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto line = Trim(lines[i]);
        if (line.size() > 2 && line.front() == '[' && line.back() == ']') {
            if (start != lines.size()) return {start, i};
            if (Lower(Trim(line.substr(1, line.size() - 2))) == Lower(section)) start = i;
        }
    }
    return {start, lines.size()};
}
void EnsureSection(Lines& lines, const std::string& section) {
    if (Span(lines, section).first != lines.size()) return;
    while (!lines.empty() && Trim(lines.back()).empty()) lines.pop_back();
    if (!lines.empty()) lines.emplace_back();
    lines.push_back("[" + section + "]");
}
std::vector<std::size_t> ValueHits(const Lines& lines, std::size_t start, std::size_t end, const std::string& key) {
    std::vector<std::size_t> hits;
    for (auto i = start + 1; i < end; ++i) {
        const auto raw = lines[i];
        const auto first = raw.find_first_not_of(" \t");
        if (first == std::string::npos || raw[first] == ';' || raw[first] == '#') continue;
        const auto equal = raw.find('=');
        if (equal != std::string::npos && Lower(Trim(raw.substr(first, equal - first))) == Lower(key)) hits.push_back(i);
    }
    return hits;
}
void RemoveSection(Lines& lines, const std::string& section) {
    auto [start, end] = Span(lines, section);
    if (start == lines.size()) return;
    while (start > 0 && Trim(lines[start - 1]).empty()) --start;
    lines.erase(lines.begin() + start, lines.begin() + end);
}
std::string IniValue(const Lines& lines, const std::string& section, const std::string& key) {
    const auto [start, end] = Span(lines, section);
    if (start == lines.size()) Fail("missing config section [" + section + "]");
    std::vector<std::string> values;
    for (auto i : ValueHits(lines, start, end, key)) {
        const auto equal = lines[i].find('=');
        values.push_back(Trim(lines[i].substr(equal + 1)));
    }
    if (values.size() != 1) Fail("expected one [" + section + "] " + key + ", found " + std::to_string(values.size()));
    return values.front();
}
void Set(Lines& lines, const std::string& section, const std::string& key, const std::string& value) {
    EnsureSection(lines, section);
    auto [start, end] = Span(lines, section);
    auto hits = ValueHits(lines, start, end, key);
    if (hits.size() > 1) Fail("ambiguous scalar config value [" + section + "] " + key);
    const auto replacement = key + "=" + value;
    if (!hits.empty()) lines[hits.front()] = replacement;
    else {
        auto insert = end;
        while (insert > start + 1 && Trim(lines[insert - 1]).empty()) --insert;
        lines.insert(lines.begin() + insert, replacement);
    }
}
void Remove(Lines& lines, const std::string& section, const std::string& key, bool all = false) {
    const auto [start, end] = Span(lines, section);
    if (start == lines.size()) return;
    auto hits = ValueHits(lines, start, end, key);
    if (hits.size() > 1 && !all) Fail("ambiguous scalar config value [" + section + "] " + key);
    for (auto it = hits.rbegin(); it != hits.rend(); ++it) lines.erase(lines.begin() + *it);
}
std::string RetailValue(const Lines& retail, const std::string& section, const std::string& key) { return IniValue(retail, section, key); }
std::string Encode(Lines lines) {
    while (!lines.empty() && Trim(lines.back()).empty()) lines.pop_back();
    std::string result;
    for (std::size_t i = 0; i < lines.size(); ++i) { if (i) result += "\r\n"; result += lines[i]; }
    return result;
}
void ValidateDefault(const Lines& lines, Profile profile) {
    const bool retail = profile != Profile::Safe;
    const std::vector<std::tuple<std::string, std::string, std::string>> expected = {
        {"Engine.Engine", "GameRenderDevice", "XOpenGLDrv.XOpenGLRenderDevice"},
        {"Engine.Engine", "WindowedRenderDevice", "XOpenGLDrv.XOpenGLRenderDevice"},
        {"Engine.Engine", "RenderDevice", "XOpenGLDrv.XOpenGLRenderDevice"},
        {"Engine.Engine", "ViewportManager", "SDLDrv.SDLClient"},
        {"Engine.Engine", "AudioDevice", "ALAudio.ALAudioSubsystem"},
        {"Engine.Engine", "Language", retail ? "usa" : "int"},
        {"Engine.GameEngine", "FrameRateLimit", "144.000000"},
        {"SDLDrv.SDLClient", "NativeText", "True"}
    };
    for (const auto& [section, key, value] : expected) {
        const auto actual = IniValue(lines, section, key);
        if (Lower(actual) != Lower(value)) Fail("non-portable Default.ini [" + section + "] " + key + "=" + actual);
    }
    std::string text;
    for (const auto& line : lines) text += Lower(line) + "\n";
    if (text.find("d3d11") != std::string::npos) Fail("D3D11 leaked into generated Default.ini");
    const std::set<std::string> tupleKeys = {"x", "y", "z", "pitch", "yaw", "roll", "r", "g", "b", "a"};
    for (const auto& raw : lines) {
        const auto stripped = Trim(raw);
        const auto equal = stripped.find('=');
        if (equal != std::string::npos && tupleKeys.count(Lower(Trim(stripped.substr(0, equal))))) Fail("malformed retail tuple fragment leaked into Default.ini: " + raw);
    }
    const auto [start, end] = Span(lines, "Core.System");
    if (start == lines.size()) Fail("Default.ini template has no [Core.System]");
    for (auto i = start + 1; i < end; ++i) {
        const auto stripped = Lower(Trim(lines[i]));
        if ((Starts(stripped, "paths=") || Starts(stripped, "savepath=") || Starts(stripped, "cachepath=")) && stripped.find('\\') != std::string::npos)
            Fail("Windows path separator in generated Default.ini: " + lines[i]);
    }
    const auto [gameStart, gameEnd] = Span(lines, "Engine.GameEngine");
    if (gameStart != lines.size()) for (auto i = gameStart + 1; i < gameEnd; ++i) {
        const auto raw = lines[i];
        const auto first = raw.find_first_not_of(" \t");
        if (first != std::string::npos && Lower(raw.substr(first, 13)) == "serveractors=") Fail("unavailable ServerActors leaked into generated Default.ini");
    }
}
void ValidateDefuser(const Lines& lines, const Lines& retail) {
    const std::vector<std::string> inputKeys = {"Aliases[8]", "RightMouse", "LeftMouse", "Tab", "Enter", "Ctrl", "Alt", "Space", "Z", "Slash", "A"};
    for (const auto& key : inputKeys) if (IniValue(lines, "Engine.Input", key) != IniValue(retail, "Engine.Input", key)) Fail("retail final binding was not merged: [Engine.Input] " + key);
    if (IniValue(lines, "Engine.PlayerPawn", "bModernThirdPersonControls") != "False") Fail("Modern controls must default to False");
    if (IniValue(lines, "Engine.PlayerPawn", "ObjectDetail") != "ObjectDetailVeryHigh") Fail("max-quality ObjectDetailVeryHigh default was not applied");
    for (const auto& [key, value] : std::vector<std::pair<std::string, std::string>>{{"fDamageMultiplier_Easy", "1.2"}, {"fDamageMultiplier_Medium", "2.0"}, {"fDamageMultiplier_Hard", "3.0"}})
        if (IniValue(lines, "HGame.Harry", key) != value) Fail("retail difficulty value was not merged: " + key);
}
}

Lines DecodeIni(const fs::path& path) {
    auto data = Read(path);
    if (data.size() >= 2 && ((static_cast<unsigned char>(data[0]) == 0xff && static_cast<unsigned char>(data[1]) == 0xfe) || (static_cast<unsigned char>(data[0]) == 0xfe && static_cast<unsigned char>(data[1]) == 0xff)))
        Fail("UTF-16 config is unsupported for deterministic merge: " + Utf8(path));
    if (data.size() >= 3 && static_cast<unsigned char>(data[0]) == 0xef && static_cast<unsigned char>(data[1]) == 0xbb && static_cast<unsigned char>(data[2]) == 0xbf) data.erase(0, 3);
    ValidateUtf8(data, path);
    Lines lines;
    std::size_t start = 0;
    for (std::size_t i = 0; i < data.size();) {
        std::size_t delimiter = 0;
        const unsigned char c = static_cast<unsigned char>(data[i]);
        if (c == '\n' || c == '\v' || c == '\f' || (c >= 0x1c && c <= 0x1e)) delimiter = 1;
        else if (c == '\r') delimiter = i + 1 < data.size() && data[i + 1] == '\n' ? 2 : 1;
        else if (i + 1 < data.size() && c == 0xc2 && static_cast<unsigned char>(data[i + 1]) == 0x85) delimiter = 2;
        else if (i + 2 < data.size() && c == 0xe2 && static_cast<unsigned char>(data[i + 1]) == 0x80 &&
                 (static_cast<unsigned char>(data[i + 2]) == 0xa8 || static_cast<unsigned char>(data[i + 2]) == 0xa9))
            delimiter = 3;
        if (delimiter) {
            lines.emplace_back(data.substr(start, i - start));
            i += delimiter;
            start = i;
        } else {
            ++i;
        }
    }
    if (start < data.size()) lines.emplace_back(data.substr(start));
    return lines;
}
void SetIniValue(Lines& lines, const std::string& section, const std::string& key, const std::string& value) { Set(lines, section, key, value); }
void RemoveIniKey(Lines& lines, const std::string& section, const std::string& key, bool all) { Remove(lines, section, key, all); }
std::string EncodeIni(Lines lines) { return Encode(std::move(lines)); }

std::string BuildDefaultIni(const fs::path& templatePath, const fs::path& retailPath, Profile profile) {
    auto lines = DecodeIni(templatePath);
    auto retail = DecodeIni(retailPath);
    if (profile != Profile::Safe) {
        Remove(lines, "Engine.Engine", "EASplashWaitTime");
        Remove(lines, "Engine.Engine", "WBSplashWaitTime");
        for (const auto& key : {"EASplashWaitTime", "WBSplashWaitTime", "LaunchCode", "UseSaveSlot"}) Set(lines, "FrontEnd", key, RetailValue(retail, "FrontEnd", key));
    }
    if (profile == Profile::RetailOnly) { RemoveSection(lines, "D3D11Drv.D3D11RenderDevice"); RemoveSection(lines, "Editor.EditorEngine"); }
    const std::vector<std::pair<std::string, std::string>> engine = {
        {"GameRenderDevice", "XOpenGLDrv.XOpenGLRenderDevice"}, {"AudioDevice", "ALAudio.ALAudioSubsystem"},
        {"WindowedRenderDevice", "XOpenGLDrv.XOpenGLRenderDevice"}, {"RenderDevice", "XOpenGLDrv.XOpenGLRenderDevice"},
        {"ViewportManager", "SDLDrv.SDLClient"}, {"Language", profile == Profile::Safe ? "int" : "usa"}
    };
    for (const auto& [key, value] : engine) Set(lines, "Engine.Engine", key, value);
    Set(lines, "Engine.GameEngine", "FrameRateLimit", "144.000000");
    Remove(lines, "Engine.GameEngine", "ServerActors", true);
    Set(lines, "Core.System", "SavePath", "../Save");
    Set(lines, "Core.System", "CachePath", "../Cache");
    const std::vector<std::string> paths = {"../System/*.u", "../Maps/*.unr", "../Textures/*.utx", "../Sounds/*.uax", "../Music/*.umx", "../save/*.usa"};
    auto [start, end] = Span(lines, "Core.System");
    if (start == lines.size()) Fail("Default.ini template has no [Core.System]");
    auto hits = ValueHits(lines, start, end, "Paths");
    for (auto it = hits.rbegin(); it != hits.rend(); ++it) lines.erase(lines.begin() + *it);
    std::tie(start, end) = Span(lines, "Core.System");
    auto insert = end;
    while (insert > start + 1 && Trim(lines[insert - 1]).empty()) --insert;
    for (const auto& path : paths) lines.insert(lines.begin() + insert++, "Paths=" + path);
    const std::vector<std::pair<std::string, std::string>> sdl = {
        {"WindowedViewportX", "800"}, {"WindowedViewportY", "600"}, {"WindowedColorBits", "32"}, {"FullscreenViewportX", "800"},
        {"FullscreenViewportY", "600"}, {"FullscreenColorBits", "32"}, {"Brightness", "0.400000"}, {"UseJoystick", "True"},
        {"CaptureMouse", "True"}, {"StartupFullscreen", "False"}, {"BorderlessWindow", "False"}, {"UseDesktopResolution", "False"},
        {"AllowUnicodeKeys", "True"}, {"NativeText", "True"}, {"AllowCommandQKeys", "True"}, {"MacKeepAllScreensOn", "False"}
    };
    for (const auto& [key, value] : sdl) Set(lines, "SDLDrv.SDLClient", key, value);
    const std::vector<std::pair<std::string, std::string>> gl = {
        {"OpenGLVersion", "Core"}, {"UseBindlessTextures", "False"}, {"UsePersistentBuffers", "False"}, {"UseShaderDrawParameters", "False"},
        {"UseBufferInvalidation", "False"}, {"UseShaderCache", "False"}, {"DetailTextures", "True"}, {"MacroTextures", "True"},
        {"UseTrilinear", "True"}, {"UsePrecache", "True"}, {"UseVSync", "Off"}, {"UseAA", "True"}, {"NumAASamples", "4"}, {"MaxAnisotropy", "16.000000"}
    };
    for (const auto& [key, value] : gl) Set(lines, "XOpenGLDrv.XOpenGLRenderDevice", key, value);
    if (profile != Profile::Safe) {
        Set(lines, "ALAudio.ALAudioSubsystem", "MusicVolume", RetailValue(retail, "ALAudio.ALAudioSubsystem", "MusicVolume"));
        Set(lines, "HGame.baseConsole", "bDebugMode", "False");
        Set(lines, "HGame.HPConsole", "bDebugMode", "False");
    }
    ValidateDefault(lines, profile);
    return Encode(std::move(lines));
}

std::string BuildDefUserIni(const fs::path& templatePath, const fs::path& retailPath, Profile profile) {
    auto lines = DecodeIni(templatePath);
    Set(lines, "Engine.PlayerPawn", "bModernThirdPersonControls", "False");
    Set(lines, "Engine.PlayerPawn", "ObjectDetail", "ObjectDetailVeryHigh");
    if (profile == Profile::Safe) return Encode(std::move(lines));
    auto retail = DecodeIni(retailPath);
    for (const auto& key : {"Aliases[8]", "RightMouse", "LeftMouse", "Tab", "Enter", "Ctrl", "Alt", "Space", "Z", "Slash", "A"})
        Set(lines, "Engine.Input", key, RetailValue(retail, "Engine.Input", key));
    for (const auto& key : {"fDamageMultiplier_Easy", "fDamageMultiplier_Medium", "fDamageMultiplier_Hard"})
        Set(lines, "HGame.Harry", key, RetailValue(retail, "HGame.Harry", key));
    if (profile == Profile::Full) for (const auto& key : {"Joy1", "Joy2", "Joy3", "Joy4", "JoyY", "JoyU"})
        if (IniValue(lines, "Engine.Input", key) != IniValue(DecodeIni(templatePath), "Engine.Input", key)) Fail("controller fallback was not preserved: [Engine.Input] " + std::string(key));
    ValidateDefuser(lines, retail);
    return Encode(std::move(lines));
}
}
