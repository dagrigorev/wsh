#include "tiny_reasoning_model.h"
#include <cstring>
#include <algorithm>
#include <cctype>
#include <windows.h>

namespace wsh {

TinyReasoningModel::TinyReasoningModel() {}
TinyReasoningModel::~TinyReasoningModel() {}

bool TinyReasoningModel::Init() {
    loaded_ = true;
    return true;
}

/* ─── Edit Distance ──────────────────────────────────────────────────────────── */

int TinyReasoningModel::EditDistance(const std::string& a, const std::string& b) {
    size_t n = a.size(), m = b.size();
    std::vector<int> prev(m + 1), curr(m + 1);
    for (size_t j = 0; j <= m; j++) prev[j] = (int)j;
    for (size_t i = 1; i <= n; i++) {
        curr[0] = (int)i;
        for (size_t j = 1; j <= m; j++) {
            int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            curr[j] = std::min({ prev[j] + 1, curr[j - 1] + 1, prev[j - 1] + cost });
        }
        prev.swap(curr);
    }
    return prev[m];
}

/* ─── Typo correction table ──────────────────────────────────────────────────── */

struct TypoEntry {
    const char* wrong;
    const char* fix;
    int min_len;
};

static const TypoEntry kTypoTable[] = {
    {"gti",    "git",      2}, {"gitt",   "git",      3},
    {"gti ",   "git ",     3}, {"gi t",   "git",      3},
    {"got",    "git",      2}, {"stauts", "status",   3},
    {"staus",  "status",   3}, {"statis", "status",   4},
    {"statsu", "status",   4}, {"gihub",  "github",   3},
    {"mkae",   "make",     2}, {"mke",    "make",     2},
    {"maek",   "make",     2}, {"buiild", "build",    3},
    {"buid",   "build",    2}, {"cmkae",  "cmake",    3},
    {"cmke",   "cmake",    3}, {"dkcer",  "docker",   3},
    {"docer",  "docker",   3}, {"dcoker", "docker",   3},
    {"pythno", "python",   3}, {"pythin", "python",   3},
    {"pytohn", "python",   3}, {"pythn",  "python",   3},
    {"npn",    "npm",      2}, {"nmp",    "npm",      2},
    {"gerp",   "grep",     2}, {"chlod",  "chmod",    3},
    {"chomd",  "chmod",    3}, {"cliear", "clear",    3},
    {"clera",  "clear",    3}, {"sodu",   "sudo",     2},
    {"suod",   "sudo",     2}, {"sl",     "ls",       1},
    {"dc",     "cd",       1}, {"cd..",   "cd ..",    3},
    {"cd...",  "cd ..",    3}, {"pwoerd", "power",    3},
    {"ipconifg", "ipconfig", 4}, {"ipcnofig", "ipconfig", 4},
    {"pingg",  "ping",     3}, {"tracer", "tracert",  4},
    {"netsat", "netstat",  4}, {"nestsat","netstat",  4},
    {"tasskill","taskkill",5}, {"cal",    "calc",     2},
    {"clac",   "calc",     2}, {"chkdsk", "chkdsk",   4},
    {NULL, NULL, 0}
};

/* ─── Completion suggestion table ────────────────────────────────────────────── */

struct CmdSuggestion {
    const char* prefix;
    const char* completion;
    int min_len;
};

static const CmdSuggestion kCmdTable[] = {
    {"git ",   "git status",          3},
    {"git s",  "git status",          5},
    {"git a",  "git add .",           5},
    {"git c",  "git commit -m \"\"",  5},
    {"git p",  "git push",            5},
    {"git pu", "git push origin HEAD",6},
    {"git pl", "git pull",            6},
    {"git l",  "git log --oneline",   5},
    {"git b",  "git branch",          5},
    {"git ch", "git checkout",        6},
    {"git d",  "git diff",            5},
    {"git cl", "git clone",           6},
    {"cmake ", "cmake -B build",      5},
    {"npm i",  "npm install",         4},
    {"npm r",  "npm run",             5},
    {"npm s",  "npm start",           5},
    {"npm t",  "npm test",            5},
    {"npm b",  "npm run build",       5},
    {"npm d",  "npm run dev",         5},
    {"docker ","docker ps",           6},
    {"docker p","docker ps -a",       8},
    {"docker r","docker run",         8},
    {"docker s","docker start",       8},
    {"docker i","docker images",      8},
    {"pip i",  "pip install",         5},
    {"pip fr", "pip freeze",          6},
    {"conda i","conda install",       6},
    {"dotnet ","dotnet build",        7},
    {"dotnet b","dotnet build",       8},
    {"dotnet t","dotnet test",        8},
    {"dotnet r","dotnet run",         8},
    {"npx c",  "npx create-react-app",5},
    {"yarn ",  "yarn install",        4},
    {"yarn a", "yarn add",            5},
    {"yarn r", "yarn run",            5},
    {"yarn s", "yarn start",          5},
    {"yarn t", "yarn test",           5},
    {"yarn b", "yarn build",          5},
    {"python ", "python --version",   7},
    {"sudo a", "sudo apt install",    6},
    {"apt ",   "apt list --upgradable",3},
    {"brew ",  "brew update",         4},
    {"brew i", "brew install",        6},
    {"ssh ",   "ssh user@host",       3},
    {"scoop i","scoop install",       7},
    {"winget s","winget search",      8},
    {"choco i","choco install",       7},
    {"code .", "code .",              4},
    {"ls -l",  "ls -la",              5},
    {"ls -a",  "ls -la",              5},
    {"rm -",   "rm -rf",              3},
    {"cp -r",  "cp -r",               3},
    {"chmod ", "chmod +x",            5},
    {"tar -x", "tar -xzf",            5},
    {"find .", "find . -name \"\"",   6},
    {"grep -", "grep -r \"\" .",      6},
    {NULL, NULL, 0}
};

/* ─── Common commands for edit-distance check ───────────────────────────────── */

static const char* kCommonCommands[] = {
    "git", "npm", "make", "cmake", "ls", "cd", "cat", "rm", "mv", "cp",
    "mkdir", "grep", "sudo", "ssh", "ping", "curl", "wget", "clear", "exit",
    "python", "docker", "node", "code", "pip", "conda", "dotnet", "npx",
    "yarn", "scoop", "winget", "choco", "apt", "brew", "powershell", "pwsh",
    "chmod", "chown", "tar", "find", "touch", "head", "tail", "less", "more",
    "echo", "export", "source", "env", "which", "where", "man", "help",
    "ipconfig", "netstat", "taskkill", "taskmgr", "calc", "notepad",
    "regedit", "chkdsk", "ping", "tracert", "nslookup", "pwd",
    NULL
};

/* ─── Helper: extract first word of command ─────────────────────────────────── */

static std::string FirstWord(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_first_of(" \t\r\n", start);
    return s.substr(start, end - start);
}

/* ─── IsLikelyTypo ───────────────────────────────────────────────────────────── */

bool TinyReasoningModel::IsLikelyTypo(const std::string& input,
                                       std::string* correction) {
    if (input.empty() || input.size() < 2) return false;

    std::string trimmed = input;
    trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
    trimmed.erase(trimmed.find_last_not_of(" \t\r\n") + 1);

    if (trimmed.empty()) return false;

    /* 1. Check exact pattern table */
    for (int i = 0; kTypoTable[i].wrong != NULL; i++) {
        if ((int)trimmed.size() >= kTypoTable[i].min_len) {
            size_t pos = trimmed.find(kTypoTable[i].wrong);
            if (pos == 0) {
                if (correction) {
                    std::string rest;
                    std::string first = trimmed;
                    size_t sp = trimmed.find(' ');
                    if (sp != std::string::npos) {
                        first = trimmed.substr(0, sp);
                        rest = trimmed.substr(sp);
                    }
                    *correction = kTypoTable[i].fix;
                    if (strlen(kTypoTable[i].fix) >= first.size() ||
                        strlen(kTypoTable[i].fix) >= 2) {
                        /* If the fix is a standalone command word */
                        if (rest.empty()) {
                            *correction = kTypoTable[i].fix;
                        } else {
                            *correction = std::string(kTypoTable[i].fix) + rest;
                        }
                    }
                }
                return true;
            }
        }
    }

    /* 2. Edit-distance check */
    std::string first = FirstWord(trimmed);
    if (first.size() < 2) return false;

    /* If first word IS a known command, it's not a typo */
    {
        bool is_known = false;
        for (int i = 0; kCommonCommands[i] != NULL; i++) {
            if (first == kCommonCommands[i]) { is_known = true; break; }
        }
        if (is_known) return false;
    }

    for (int i = 0; kCommonCommands[i] != NULL; i++) {
        std::string cmd = kCommonCommands[i];
        if (first == cmd) continue;  /* not a typo (redundant with above check) */
        if (first.size() != cmd.size() &&
            std::abs((int)(first.size() - cmd.size())) > 2) continue;

        int dist = EditDistance(first, cmd);
        /* Allow 1 edit for short cmds, 2 for longer */
        int max_dist = (cmd.size() <= 3) ? 1 : 2;
        if (dist > 0 && dist <= max_dist) {
            if (correction) {
                *correction = cmd + trimmed.substr(first.size());
            }
            return true;
        }
    }

    return false;
}

/* ─── SuggestCompletion ──────────────────────────────────────────────────────── */

std::string TinyReasoningModel::SuggestCompletion(const std::string& partial) {
    if (partial.empty() || partial.size() < 2) return "";

    std::string trimmed = partial;
    size_t start = trimmed.find_first_not_of(" \t\r\n");
    if (start != std::string::npos) trimmed = trimmed.substr(start);

    int len = (int)trimmed.size();
    std::string best_match;
    int best_score = -1;

    for (int i = 0; kCmdTable[i].prefix != NULL; i++) {
        const char* prefix = kCmdTable[i].prefix;
        int prefix_len = (int)strlen(prefix);

        if (len < kCmdTable[i].min_len) continue;

        /* Exact prefix match */
        if (strncmp(prefix, trimmed.c_str(), (size_t)len) == 0 && len <= prefix_len) {
            if (len > best_score) {
                best_score = len;
                best_match = kCmdTable[i].completion;
            }
        }

        /* Input is a prefix of a longer suggestion prefix */
        if (len < prefix_len &&
            strncmp(trimmed.c_str(), prefix, (size_t)len) == 0) {
            if (len > best_score) {
                best_score = len;
                best_match = kCmdTable[i].completion;
            }
        }
    }

    return best_match;
}

/* ─── Analyze ───────────────────────────────────────────────────────────────────
 * Returns friendly conversational reasoning (shown as overlay in terminal). */

static std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string TinyReasoningModel::Analyze(const std::string& input) {
    if (!loaded_ || input.empty()) return "";
    cancel_.store(false);

    std::string t = Trim(input);
    if (t.empty()) return "";

    /* 1. Friendly typo correction */
    std::string correction;
    if (IsLikelyTypo(t, &correction)) {
        std::string first = FirstWord(t);
        if (first == "sl") return "Typo! `sl` → `ls`. Trying to list files?";
        if (first == "dc")  return "Typo! `dc` → `cd`. Going somewhere?";
        if (first == "gti") return "Typo! `gti` → `git`. Git commands? I can help!";
        if (first == "npn" || first == "nmp") return "Typo! Did you mean `npm`?";
        if (first == "grep" || first == "gerp") return "Typo! Did you mean `grep`? Searching files?";
        return "Hmm, that looks like a typo! Did you mean `" + correction + "`?";
    }

    /* 2. Command in progress — suggest completion with friendly context */
    std::string completion = SuggestCompletion(t);
    if (!completion.empty()) {
        std::string first = FirstWord(t);
        if (first == "git")  return "Git commands? Try `" + completion + "`";
        if (first == "npm")  return "Managing packages? Try `" + completion + "`";
        if (first == "docker") return "Docker? Try `" + completion + "`";
        if (first == "cmake" || first.find("make") == 0) return "Building? Try `" + completion + "`";
        return "Need a hand? Try `" + completion + "`";
    }

    /* 3. Friendly reactions for known commands being typed */
    std::string first = FirstWord(t);
    if (!first.empty()) {
        if (first == "ls")    return "You want to see a list of files?";
        if (first == "cd")    return "Changing directories? Where to?";
        if (first == "cat")   return "Reading a file? I can help you find it.";
        if (first == "clear" || first == "cls") return "Clearing the screen!";
        if (first == "echo")  return "Printing something?";
        if (first == "pwd")   return "Wondering where you are?";
        if (first == "exit")  return "Leaving so soon?";
        if (first == "help")  return "Need help? I'm here!";
        if (first == "mkdir") return "Creating a new directory?";
        if (first == "rm")    return "Deleting something? Be careful!";
        if (first == "cp")    return "Copying files?";
        if (first == "mv")    return "Moving or renaming?";
        if (first == "ping")  return "Testing connectivity?";
        if (first == "find")  return "Searching for files?";
        if (first == "grep")  return "Searching inside files?";
        if (first == "python" || first == "python3") return "Running Python? Awesome!";
        if (first == "code")  return "Opening VS Code? Great editor!";
        if (first == "ssh")   return "Connecting remotely?";
        if (first == "curl" || first == "wget") return "Fetching from the web?";
        if (first == "man" || first == "help") return "Need help with a command?";
        if (first == "touch") return "Creating a new file?";
        if (first == "head" || first == "tail") return "Peeking at a file?";
        if (first == "which" || first == "where") return "Finding where a command lives?";
        if (first == "sudo")  return "Need admin privileges? Careful!";
        if (first == "npm")   return "Managing packages? Try `npm install`";
        if (first == "make")  return "Building with Make? Try `make`";
        if (first == "yarn")  return "Using Yarn? Try `yarn install`";
        if (first == "pip")   return "Installing Python packages?";
        if (first == "apt" || first == "brew" || first == "scoop" || first == "winget" || first == "choco") return "Installing something? Awesome!";
        if (first == "dotnet") return "Building with .NET? Nice!";
        if (first == "where") return "Finding where a command lives?";
        if (first == "ipconfig" || first == "netstat") return "Checking network info?";
        if (first == "calc" || first == "notepad" || first == "notepad++" || first == "regedit") return "Opening a Windows tool?";
        if (first == "chmod" || first == "chown") return "Changing permissions?";
        if (first == "tar")   return "Extracting an archive?";
        if (first == "export" || first == "set") return "Setting environment variables?";
        if (first == "source") return "Sourcing a script?";
        if (first == "history" || first == "hc") return "Checking your command history?";
    }

    /* 4. Partial input — check if it prefixes a known command */
    if (t.size() >= 2) {
        for (int i = 0; kCommonCommands[i] != NULL; i++) {
            std::string cmd = kCommonCommands[i];
            if (cmd.find(t) == 0) {
                return "Typing `" + cmd + "`? I can help with that!";
            }
        }
    }

    return "";
}

}  /* namespace wsh */
