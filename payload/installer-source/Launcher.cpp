#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <iostream>
#include <string>
#include <vector>

static std::wstring Quote(const std::wstring& value) {
    std::wstring result = L"\"";
    unsigned slashes = 0;
    for (wchar_t ch : value) {
        if (ch == L'\\') { ++slashes; continue; }
        result.append(ch == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        result += ch;
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    return result + L"\"";
}

static bool FileExists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static int Finish(int code) {
    std::wcout << L"\nPress Enter to close this window.\n";
    std::wstring input;
    std::getline(std::wcin, input);
    return code;
}

int wmain(int argc, wchar_t** argv) {
    SetConsoleTitleW(L"NightShift installer");
    std::wcout << L"NightShift 0.5.1\n"
                  L"Nivalis Nights community mod\n\n";
    if (argc > 2) {
        std::wcout << L"Open this application to install, or drag your Nivalis Nights game folder onto it.\n";
        return Finish(1);
    }
    std::vector<wchar_t> module(32768);
    const DWORD length = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
    if (!length || length >= module.size()) {
        std::wcout << L"Couldn't find the installer folder. Extract the complete ZIP and try again.\n";
        return Finish(1);
    }
    const std::wstring modulePath(module.data(), length);
    const auto separator = modulePath.find_last_of(L"\\/");
    if (separator == std::wstring::npos) return Finish(1);
    const std::wstring directory = modulePath.substr(0, separator);
    const std::wstring script = directory + L"\\payload\\install.ps1";
    if (!FileExists(script) || !FileExists(directory + L"\\payload\\NightShift.dll")
            || !FileExists(directory + L"\\payload\\release.json")
            || !FileExists(directory + L"\\payload\\BepInEx-788.zip")) {
        std::wcout << L"The NightShift package isn't fully extracted.\n\n"
                      L"1. Close this window.\n"
                      L"2. In File Explorer, right-click the downloaded ZIP and choose Extract All.\n"
                      L"3. Click Extract, then open the extracted NightShift-0.5.1 folder.\n"
                      L"4. Open Install NightShift.exe there.\n\n"
                      L"Keep the payload and Tools folders beside this application.\n";
        return Finish(1);
    }
    wchar_t systemDirectory[MAX_PATH];
    const UINT systemLength = GetSystemDirectoryW(systemDirectory, MAX_PATH);
    if (!systemLength || systemLength >= MAX_PATH) return Finish(1);
    const std::wstring powershell = std::wstring(systemDirectory) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
    if (!FileExists(powershell)) {
        std::wcout << L"Windows PowerShell wasn't found. This installer needs Windows PowerShell 5.1.\n";
        return Finish(1);
    }
    std::wstring command = Quote(powershell) + L" -NoProfile -ExecutionPolicy Bypass -File " + Quote(script);
    if (argc == 2) command += L" -GamePath " + Quote(argv[1]);
    std::vector<wchar_t> commandBuffer(command.begin(), command.end());
    commandBuffer.push_back(L'\0');

    // Let Windows PowerShell use its own modules, including when launched from
    // a PowerShell 7 terminal. Change only the child environment.
    std::vector<wchar_t> environment;
    wchar_t* inherited = GetEnvironmentStringsW();
    if (!inherited) return Finish(1);
    for (const wchar_t* entry = inherited; *entry; entry += wcslen(entry) + 1) {
        if (_wcsnicmp(entry, L"PSModulePath=", 13) == 0) continue;
        environment.insert(environment.end(), entry, entry + wcslen(entry) + 1);
    }
    FreeEnvironmentStringsW(inherited);
    environment.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION child{};
    std::wcout << L"Checking your game and installing NightShift...\n\n" << std::flush;
    if (!CreateProcessW(powershell.c_str(), commandBuffer.data(), nullptr, nullptr, TRUE,
            CREATE_UNICODE_ENVIRONMENT, environment.data(), directory.c_str(), &startup, &child)) {
        std::wcout << L"Couldn't start setup. Windows error: " << GetLastError() << L".\n";
        return Finish(1);
    }
    WaitForSingleObject(child.hProcess, INFINITE);
    DWORD result = 1;
    GetExitCodeProcess(child.hProcess, &result);
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
    if (result == 0) {
        std::wcout << L"\nInstalled. Launch Nivalis Nights through Steam and load your save.\n"
                      L"F11: frame generation. F5: DLAA. F7: show or hide the menu.\n";
    } else {
        std::wcout << L"\nSetup stopped. Read the message above before trying again.\n"
                      L"If it says access is denied, right-click Install NightShift.exe\n"
                      L"and choose Run as administrator.\n";
    }
    return Finish(static_cast<int>(result));
}
