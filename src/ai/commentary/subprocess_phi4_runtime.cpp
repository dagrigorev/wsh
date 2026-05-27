#include "subprocess_phi4_runtime.h"
#include "../../core/log.h"
#include <windows.h>
#include <cstdio>

namespace wsh {

static std::string TrimWhitespace(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static void StripAnsi(std::string& s) {
    std::string clean;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\x1b' && i + 1 < s.size() && s[i + 1] == '[') {
            i += 2;
            while (i < s.size() && !(s[i] >= 'A' && s[i] <= 'Z') &&
                   !(s[i] >= 'a' && s[i] <= 'z')) {
                ++i;
            }
        } else {
            clean += s[i];
        }
    }
    s = std::move(clean);
}

SubprocessPhi4Runtime::SubprocessPhi4Runtime() {}
SubprocessPhi4Runtime::~SubprocessPhi4Runtime() { Shutdown(); }

std::string SubprocessPhi4Runtime::FindExe() const {
    char exe_path[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, exe_path, MAX_PATH);
    std::string exe_dir = exe_path;
    size_t bs = exe_dir.find_last_of('\\');
    if (bs != std::string::npos) exe_dir = exe_dir.substr(0, bs + 1);

    std::string candidates[] = {
        exe_dir + "llama-completion.exe",
        exe_dir + "bin\\llama-completion.exe",
        "llama-completion.exe"
    };

    for (const auto& path : candidates) {
        DWORD attrs = GetFileAttributesA(path.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            char full[MAX_PATH] = {0};
            GetFullPathNameA(path.c_str(), MAX_PATH, full, NULL);
            WSH_LOG_INFO("SubprocessPhi4Runtime: found exe at %s", full);
            return full;
        }
    }

    char buf[32768];
    DWORD ret = SearchPathA(NULL, "llama-completion", ".exe", sizeof(buf), buf, NULL);
    if (ret > 0 && ret < sizeof(buf)) {
        WSH_LOG_INFO("SubprocessPhi4Runtime: found llama-completion in PATH at %s", buf);
        return buf;
    }

    return "";
}

bool SubprocessPhi4Runtime::Initialize(const Phi4Config& config) {
    config_ = config;
    initialized_ = true;

    if (config_.model_path.empty()) {
        WSH_LOG_WARN("SubprocessPhi4Runtime: no model path configured");
        return true;
    }

    DWORD attrs = GetFileAttributesA(config_.model_path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        WSH_LOG_WARN("SubprocessPhi4Runtime: model not found at %s", config_.model_path.c_str());
        return true;
    }

    WSH_LOG_INFO("SubprocessPhi4Runtime: model file found at %s", config_.model_path.c_str());

    exe_path_ = FindExe();
    if (exe_path_.empty()) {
        WSH_LOG_WARN("SubprocessPhi4Runtime: llama-completion.exe not found, runtime unavailable");
        return true;
    }

    if (!SpawnProcess()) {
        WSH_LOG_WARN("SubprocessPhi4Runtime: failed to spawn subprocess");
        return true;
    }

    cli_available_ = true;
    WSH_LOG_INFO("SubprocessPhi4Runtime: initialized with persistent subprocess");
    return true;
}

bool SubprocessPhi4Runtime::SpawnProcess() {
    /* Create stdin pipe (child reads from hStdinRead, we write to hStdinWrite) */
    HANDLE hStdinRead, hStdinWrite;
    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
    if (!CreatePipe(&hStdinRead, &hStdinWrite, &sa, 0)) {
        WSH_LOG_WARN("SubprocessPhi4Runtime: CreatePipe stdin failed");
        return false;
    }
    SetHandleInformation(hStdinWrite, HANDLE_FLAG_INHERIT, 0);

    /* Create stdout pipe (child writes to hStdoutWrite, we read from hStdoutRead) */
    HANDLE hStdoutRead, hStdoutWrite;
    if (!CreatePipe(&hStdoutRead, &hStdoutWrite, &sa, 0)) {
        WSH_LOG_WARN("SubprocessPhi4Runtime: CreatePipe stdout failed");
        CloseHandle(hStdinRead);
        CloseHandle(hStdinWrite);
        return false;
    }
    SetHandleInformation(hStdoutRead, HANDLE_FLAG_INHERIT, 0);

    HANDLE hNullErr = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  NULL, OPEN_EXISTING, 0, NULL);

    /* Build args for interactive mode */
    std::string args = "\"" + exe_path_ + "\"";
    args += " -m \"" + config_.model_path + "\"";
    args += " -n " + std::to_string(config_.max_tokens);
    args += " -c " + std::to_string(config_.context_tokens);
    args += " --color off";
    args += " --temp " + std::to_string(config_.temperature);

    PROCESS_INFORMATION pi = {0};
    STARTUPINFOA si = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput  = hStdinRead;
    si.hStdOutput = hStdoutWrite;
    si.hStdError  = hNullErr ? hNullErr : INVALID_HANDLE_VALUE;

    char* mutable_args = _strdup(args.c_str());
    BOOL ok = CreateProcessA(NULL, mutable_args, NULL, NULL, TRUE,
                             CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    free(mutable_args);

    /* Close our copies of the child's pipe ends */
    CloseHandle(hStdinRead);
    CloseHandle(hStdoutWrite);
    if (hNullErr) CloseHandle(hNullErr);

    if (!ok) {
        WSH_LOG_WARN("SubprocessPhi4Runtime: CreateProcess failed");
        CloseHandle(hStdinWrite);
        CloseHandle(hStdoutRead);
        return false;
    }

    CloseHandle(pi.hThread);
    hProcess_   = pi.hProcess;
    hStdinWrite_ = hStdinWrite;
    hStdoutRead_ = hStdoutRead;

    /* Wait for model to finish loading (watch for first '> ' prompt) */
    std::string startup;
    char buf[4096];
    DWORD read_bytes = 0;
    int waitMs = 60000; /* Allow up to 60s for model load */

    while (waitMs > 0) {
        DWORD waitResult = WaitForSingleObject(hProcess_, 100);
        if (waitResult == WAIT_OBJECT_0) {
            WSH_LOG_WARN("SubprocessPhi4Runtime: process exited during load");
            KillProcess();
            return false;
        }

        while (PeekNamedPipe(hStdoutRead_, NULL, 0, NULL, &read_bytes, NULL) && read_bytes > 0) {
            if (ReadFile(hStdoutRead_, buf, sizeof(buf) - 1, &read_bytes, NULL) && read_bytes > 0) {
                buf[read_bytes] = '\0';
                startup += buf;
            }
        }

        /* Check for prompt marker '\n> ' at end of buffer */
        size_t slen = startup.size();
        if (slen >= 3 && startup[slen - 3] == '\n' && startup[slen - 2] == '>' && startup[slen - 1] == ' ') {
            WSH_LOG_INFO("SubprocessPhi4Runtime: model loaded (startup: %zu bytes)", startup.size());
            return true;
        }

        waitMs -= 100;
    }

    WSH_LOG_WARN("SubprocessPhi4Runtime: timeout waiting for model load");
    KillProcess();
    return false;
}

std::string SubprocessPhi4Runtime::Generate(const std::string& prompt, int maxTokens, int timeoutMs) {
    if (!cli_available_ || prompt.empty()) return "";

    std::lock_guard<std::mutex> lock(pipe_mutex_);

    /* Check if process is still alive */
    if (hProcess_ && WaitForSingleObject(hProcess_, 0) == WAIT_OBJECT_0) {
        WSH_LOG_WARN("SubprocessPhi4Runtime: process died, respawning");
        KillProcess();
        if (!SpawnProcess()) return "";
    }

    if (!hStdinWrite_ || !hStdoutRead_ || !hProcess_) {
        if (!SpawnProcess()) return "";
    }

    /* Write prompt to process stdin */
    std::string input = prompt + "\n";
    DWORD written = 0;
    WriteFile(hStdinWrite_, input.c_str(), (DWORD)input.size(), &written, NULL);
    FlushFileBuffers(hStdinWrite_);

    /* Read response from process stdout until we see '> ' prompt */
    std::string output;
    char buf[4096];
    DWORD read_bytes = 0;

    while (timeoutMs > 0) {
        DWORD waitResult = WaitForSingleObject(hProcess_, 50);
        if (waitResult == WAIT_OBJECT_0) {
            /* Process exited — read remaining output */
            while (PeekNamedPipe(hStdoutRead_, NULL, 0, NULL, &read_bytes, NULL) && read_bytes > 0) {
                if (ReadFile(hStdoutRead_, buf, sizeof(buf) - 1, &read_bytes, NULL) && read_bytes > 0) {
                    buf[read_bytes] = '\0';
                    output += buf;
                }
            }
            break;
        }

        while (PeekNamedPipe(hStdoutRead_, NULL, 0, NULL, &read_bytes, NULL) && read_bytes > 0) {
            if (ReadFile(hStdoutRead_, buf, sizeof(buf) - 1, &read_bytes, NULL) && read_bytes > 0) {
                buf[read_bytes] = '\0';
                output += buf;
            }
        }

        /* Check for end-of-response marker: '\n> ' prompt at end of output */
        size_t len = output.size();
        if (len >= 3 && output[len - 3] == '\n' && output[len - 2] == '>' && output[len - 1] == ' ') {
            output.resize(len - 3);
            break;
        }

        timeoutMs -= 50;
    }

    /* Clean up */
    output = TrimWhitespace(output);
    StripAnsi(output);

    /* Strip prompt echo if present (process may echo input) */
    if (output.size() > prompt.size() && output.compare(0, prompt.size(), prompt) == 0) {
        output = TrimWhitespace(output.substr(prompt.size()));
    }

    return output;
}

bool SubprocessPhi4Runtime::IsAvailable() const {
    return initialized_ && cli_available_;
}

bool SubprocessPhi4Runtime::IsModelLoaded() const {
    if (config_.model_path.empty()) return false;
    DWORD attrs = GetFileAttributesA(config_.model_path.c_str());
    return (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY));
}

std::string SubprocessPhi4Runtime::GetModelPath() const {
    return config_.model_path;
}

void SubprocessPhi4Runtime::KillProcess() {
    if (hProcess_) {
        TerminateProcess(hProcess_, 1);
        WaitForSingleObject(hProcess_, 5000);
        CloseHandle(hProcess_);
        hProcess_ = NULL;
    }
    if (hStdinWrite_) {
        CloseHandle(hStdinWrite_);
        hStdinWrite_ = NULL;
    }
    if (hStdoutRead_) {
        CloseHandle(hStdoutRead_);
        hStdoutRead_ = NULL;
    }
}

void SubprocessPhi4Runtime::Shutdown() {
    initialized_ = false;
    cli_available_ = false;
    KillProcess();
}

} /* namespace wsh */
