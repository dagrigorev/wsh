#include "common.hpp"
#include <fstream>
#include <sstream>
#include <cwchar>

static std::wstring base_name(std::wstring s) {
    size_t p = s.find_last_of(L"\\/");
    if (p != std::wstring::npos) s = s.substr(p + 1);
    if (s.size() > 4 && _wcsicmp(s.c_str() + s.size() - 4, L".exe") == 0) s.resize(s.size() - 4);
    return s;
}

static bool exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
static bool is_dir_path(const std::wstring& p) { DWORD a=GetFileAttributesW(p.c_str()); return a!=INVALID_FILE_ATTRIBUTES && (a&FILE_ATTRIBUTE_DIRECTORY); }

static void print_common_help(const wchar_t *name) {
    std::wcout << name << L" - Wsh core utility\n\n"
               << L"Usage: " << name << L" [options] [arguments...]\n\n"
               << L"Use: man " << name << L"\n";
}

static int cat(int argc, wchar_t **argv) {
    if (is_help(argc, argv)) { print_common_help(L"cat"); return 0; }
    if (argc == 1) { std::wcerr << L"cat: stdin mode is not implemented yet\n"; return 1; }
    int rc=0;
    for (int i=1;i<argc;i++) {
        std::ifstream f(argv[i], std::ios::binary);
        if (!f) { std::wcerr << L"cat: cannot open '" << argv[i] << L"'\n"; rc=1; continue; }
        std::cout << f.rdbuf();
    }
    return rc;
}

static int pwd(int argc, wchar_t **argv) { if (is_help(argc, argv)) { print_common_help(L"pwd"); return 0; } std::wcout << current_dir() << L"\n"; return 0; }
static int whoami(int argc, wchar_t **argv) { if (is_help(argc, argv)) { print_common_help(L"whoami"); return 0; } wchar_t u[256]{}; DWORD n=256; GetUserNameW(u,&n); std::wcout << u << L"\n"; return 0; }
static int hostname(int argc, wchar_t **argv) { if (is_help(argc, argv)) { print_common_help(L"hostname"); return 0; } wchar_t h[256]{}; DWORD n=256; GetComputerNameW(h,&n); std::wcout << h << L"\n"; return 0; }
static int uname(int argc, wchar_t **argv) { if (is_help(argc, argv)) { print_common_help(L"uname"); return 0; } OSVERSIONINFOW vi{}; vi.dwOSVersionInfoSize = sizeof(vi); GetVersionExW(&vi); std::wcout << L"Windows " << vi.dwMajorVersion << L"." << vi.dwMinorVersion << L" Wsh\n"; return 0; }
static int clear(int argc, wchar_t **argv) { if (is_help(argc, argv)) { print_common_help(L"clear"); return 0; } std::wcout << L"\x1b[2J\x1b[H"; return 0; }

static int date(int argc, wchar_t **argv) {
    if (is_help(argc, argv)) { print_common_help(L"date"); return 0; } SYSTEMTIME st{}; GetLocalTime(&st);
    wchar_t d[128]{}; GetDateFormatW(LOCALE_USER_DEFAULT, DATE_LONGDATE, &st, NULL, d, 128);
    wchar_t t[128]{}; GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &st, NULL, t, 128);
    std::wcout << d << L" " << t << L"\n"; return 0;
}

static int touch(int argc, wchar_t **argv) {
    if (argc < 2 || is_help(argc, argv)) { print_common_help(L"touch"); return argc < 2 ? 1 : 0; }
    int rc=0;
    for (int i=1;i<argc;i++) {
        HANDLE h=CreateFileW(argv[i], GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h==INVALID_HANDLE_VALUE) { std::wcerr << L"touch: cannot touch '" << argv[i] << L"'\n"; rc=1; continue; }
        FILETIME ft; GetSystemTimeAsFileTime(&ft); SetFileTime(h, NULL, NULL, &ft); CloseHandle(h);
    }
    return rc;
}

static bool copy_one(const std::wstring& src, const std::wstring& dst, bool overwrite) {
    std::wstring out = is_dir_path(dst) ? join_path(dst, base_name(src)) : dst;
    return CopyFileW(src.c_str(), out.c_str(), overwrite ? FALSE : TRUE) != 0;
}
static int cp(int argc, wchar_t **argv) {
    if (argc < 3 || is_help(argc, argv)) { print_common_help(L"cp"); return argc < 3 ? 1 : 0; }
    bool force=false; std::vector<std::wstring> args;
    for (int i=1;i<argc;i++) { std::wstring a=argv[i]; if (a==L"-f"||a==L"--force") force=true; else args.push_back(a); }
    if (args.size()<2) return 1;
    std::wstring dst=args.back(); int rc=0;
    for (size_t i=0;i+1<args.size();i++) if (!copy_one(args[i], dst, force)) { std::wcerr<<L"cp: failed '"<<args[i]<<L"'\n"; rc=1; }
    return rc;
}
static int mv(int argc, wchar_t **argv) {
    if (argc < 3 || is_help(argc, argv)) { print_common_help(L"mv"); return argc < 3 ? 1 : 0; }
    std::wstring dst=argv[argc-1]; int rc=0;
    for (int i=1;i<argc-1;i++) { std::wstring out=is_dir_path(dst)?join_path(dst,base_name(argv[i])):dst; if (!MoveFileExW(argv[i], out.c_str(), MOVEFILE_REPLACE_EXISTING|MOVEFILE_COPY_ALLOWED)) { std::wcerr<<L"mv: failed '"<<argv[i]<<L"'\n"; rc=1; } }
    return rc;
}
static int rm(int argc, wchar_t **argv) {
    if (argc < 2 || is_help(argc, argv)) { print_common_help(L"rm"); return argc < 2 ? 1 : 0; }
    bool recursive=false, force=false; int rc=0;
    for (int i=1;i<argc;i++) { std::wstring a=argv[i]; if (a==L"-r"||a==L"-R"||a==L"--recursive") recursive=true; else if (a==L"-f"||a==L"--force") force=true; else { BOOL ok=is_dir_path(a)?(recursive?RemoveDirectoryW(a.c_str()):FALSE):DeleteFileW(a.c_str()); if(!ok && !force){std::wcerr<<L"rm: cannot remove '"<<a<<L"'\n"; rc=1;} } }
    return rc;
}
static int rmdir_cmd(int argc, wchar_t **argv) { if(argc<2||is_help(argc,argv)){print_common_help(L"rmdir");return argc<2?1:0;} int rc=0; for(int i=1;i<argc;i++) if(!RemoveDirectoryW(argv[i])){std::wcerr<<L"rmdir: failed '"<<argv[i]<<L"'\n";rc=1;} return rc; }
static int mkdir_cmd(int argc, wchar_t **argv) { if(argc<2||is_help(argc,argv)){print_common_help(L"mkdir");return argc<2?1:0;} int rc=0; for(int i=1;i<argc;i++) if(!CreateDirectoryW(argv[i],NULL)&&GetLastError()!=ERROR_ALREADY_EXISTS){std::wcerr<<L"mkdir: failed '"<<argv[i]<<L"'\n";rc=1;} return rc; }

static int wc_cmd(int argc, wchar_t **argv) {
    if (argc < 2 || is_help(argc, argv)) { print_common_help(L"wc"); return argc < 2 ? 1 : 0; }
    int rc=0; for(int i=1;i<argc;i++){ std::ifstream f(argv[i],std::ios::binary); if(!f){std::wcerr<<L"wc: cannot open '"<<argv[i]<<L"'\n";rc=1;continue;} long long lines=0,words=0,bytes=0; bool inw=false; char c; while(f.get(c)){bytes++; if(c=='\n')lines++; if(isspace((unsigned char)c)) inw=false; else if(!inw){words++;inw=true;}} std::wcout<<lines<<L" "<<words<<L" "<<bytes<<L" "<<argv[i]<<L"\n";} return rc;
}
static int head_tail(int argc, wchar_t **argv, bool tail) {
    if (argc < 2 || is_help(argc, argv)) { print_common_help(tail?L"tail":L"head"); return argc < 2 ? 1 : 0; }
    int n=10, start=1; if(argc>3 && wcscmp(argv[1],L"-n")==0){n=_wtoi(argv[2]);start=3;} std::vector<std::string> lines; int rc=0;
    for(int a=start;a<argc;a++){ std::ifstream f(argv[a]); if(!f){std::wcerr<<(tail?L"tail":L"head")<<L": cannot open '"<<argv[a]<<L"'\n";rc=1;continue;} std::string line; while(std::getline(f,line)) lines.push_back(line); int from=tail?std::max(0,(int)lines.size()-n):0; int to=tail?(int)lines.size():std::min(n,(int)lines.size()); for(int i=from;i<to;i++) std::cout<<lines[i]<<"\n"; lines.clear(); } return rc;
}
static int grep(int argc, wchar_t **argv) {
    if (argc < 3 || is_help(argc, argv)) { print_common_help(L"grep"); return argc < 3 ? 1 : 0; }
    std::wstring patw=argv[1]; std::string pat(patw.begin(), patw.end()); bool found=false; int rc=0;
    for(int a=2;a<argc;a++){ std::ifstream f(argv[a]); if(!f){std::wcerr<<L"grep: cannot open '"<<argv[a]<<L"'\n";rc=2;continue;} std::string line; while(std::getline(f,line)){ if(line.find(pat)!=std::string::npos){ std::cout<<line<<"\n"; found=true; } } } return rc?rc:(found?0:1);
}
static int sort_cmd(int argc, wchar_t **argv) { if(argc<2||is_help(argc,argv)){print_common_help(L"sort");return argc<2?1:0;} std::vector<std::string> v; for(int a=1;a<argc;a++){std::ifstream f(argv[a]);std::string l;while(std::getline(f,l))v.push_back(l);} std::sort(v.begin(),v.end()); for(auto&s:v)std::cout<<s<<"\n"; return 0; }
static int uniq(int argc, wchar_t **argv) { if(argc<2||is_help(argc,argv)){print_common_help(L"uniq");return argc<2?1:0;} std::string prev,l; bool have=false; for(int a=1;a<argc;a++){std::ifstream f(argv[a]);while(std::getline(f,l)){if(!have||l!=prev){std::cout<<l<<"\n";prev=l;have=true;}}} return 0; }
static int basename_cmd(int argc, wchar_t **argv) { if(argc<2||is_help(argc,argv)){print_common_help(L"basename");return argc<2?1:0;} std::wcout<<base_name(argv[1])<<L"\n"; return 0; }
static int dirname_cmd(int argc, wchar_t **argv) { if(argc<2||is_help(argc,argv)){print_common_help(L"dirname");return argc<2?1:0;} std::wstring s=argv[1]; size_t p=s.find_last_of(L"\\/"); std::wcout<<(p==std::wstring::npos?L".":s.substr(0,p))<<L"\n"; return 0; }
static int which_cmd(int argc, wchar_t **argv) { if(argc<2||is_help(argc,argv)){print_common_help(L"which");return argc<2?1:0;} int rc=0; for(int i=1;i<argc;i++){ wchar_t buf[MAX_PATH]{}; if(SearchPathW(NULL,argv[i],L".exe",MAX_PATH,buf,NULL)) std::wcout<<buf<<L"\n"; else {rc=1; std::wcerr<<L"which: no "<<argv[i]<<L" in PATH\n";} } return rc; }
static int sleep_cmd(int argc, wchar_t **argv) { if(argc<2||is_help(argc,argv)){print_common_help(L"sleep");return argc<2?1:0;} Sleep((DWORD)(_wtof(argv[1])*1000.0)); return 0; }
static int yes_cmd(int argc, wchar_t **argv) { if (is_help(argc, argv)) { print_common_help(L"yes"); return 0; } std::wstring text=argc>1?argv[1]:L"y"; for(int i=0;i<10000;i++) std::wcout<<text<<L"\n"; return 0; }
static int true_cmd(int argc, wchar_t **argv) { if (is_help(argc, argv)) { print_common_help(L"true"); return 0; } return 0; }
static int false_cmd(int argc, wchar_t **argv) { if (is_help(argc, argv)) { print_common_help(L"false"); return 0; } return 1; }


static int echo_cmd(int argc, wchar_t **argv) { if (is_help(argc, argv)) { print_common_help(L"echo"); return 0; } for(int i=1;i<argc;i++){ if(i>1) std::wcout<<L" "; std::wcout<<argv[i]; } std::wcout<<L"\n"; return 0; }
static int env_cmd(int argc, wchar_t **argv) { if (is_help(argc, argv)) { print_common_help(L"env"); return 0; } LPWCH e=GetEnvironmentStringsW(); if(!e) return 1; for(LPWCH p=e; *p; p+=wcslen(p)+1) std::wcout<<p<<L"\n"; FreeEnvironmentStringsW(e); return 0; }
static int printenv_cmd(int argc, wchar_t **argv) { if (is_help(argc, argv)) { print_common_help(L"printenv"); return 0; } if(argc==1) return env_cmd(argc,argv); int rc=0; for(int i=1;i<argc;i++){ wchar_t b[32767]{}; DWORD n=GetEnvironmentVariableW(argv[i],b,32767); if(n) std::wcout<<b<<L"\n"; else rc=1; } return rc; }
static int more_less_cmd(int argc, wchar_t **argv, const wchar_t *name) { if (is_help(argc, argv)) { print_common_help(name); return 0; } return cat(argc, argv); }
static void find_walk(const std::wstring& root, const std::wstring& needle, int *count) { WIN32_FIND_DATAW fd{}; HANDLE h=FindFirstFileW(join_path(root,L"*").c_str(),&fd); if(h==INVALID_HANDLE_VALUE) return; do{ if(is_dots(fd.cFileName)) continue; std::wstring full=join_path(root,fd.cFileName); if(needle.empty() || std::wstring(fd.cFileName).find(needle)!=std::wstring::npos){ std::wcout<<full<<L"\n"; (*count)++; } if(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) find_walk(full,needle,count); }while(FindNextFileW(h,&fd)); FindClose(h); }
static int find_cmd(int argc, wchar_t **argv) { if (is_help(argc, argv)) { print_common_help(L"find"); return 0; } std::wstring root=L"."; std::wstring needle; for(int i=1;i<argc;i++){ std::wstring a=argv[i]; if(a==L"-name" && i+1<argc) needle=argv[++i]; else root=a; } int count=0; find_walk(root, needle, &count); return 0; }
static int tee_cmd(int argc, wchar_t **argv) { if (is_help(argc, argv)) { print_common_help(L"tee"); return 0; } std::vector<std::ofstream*> outs; for(int i=1;i<argc;i++) outs.push_back(new std::ofstream(argv[i], std::ios::binary)); std::string line; while(std::getline(std::cin,line)){ std::cout<<line<<"\n"; for(auto f:outs) if(*f) (*f)<<line<<"\n"; } for(auto f:outs){ delete f; } return 0; }
static int cut_cmd(int argc, wchar_t **argv) { if (argc < 4 || is_help(argc, argv)) { print_common_help(L"cut"); return argc < 4 ? 1 : 0; } wchar_t delim=L'\t'; int field=1; std::vector<std::wstring> files; for(int i=1;i<argc;i++){ std::wstring a=argv[i]; if(a==L"-d"&&i+1<argc) delim=argv[++i][0]; else if(a==L"-f"&&i+1<argc) field=_wtoi(argv[++i]); else files.push_back(a); } for(auto& file:files){ std::wifstream f(file.c_str()); std::wstring line; while(std::getline(f,line)){ int cur=1; size_t start=0; for(size_t pos=0;;pos++){ if(pos==line.size() || line[pos]==delim){ if(cur==field){ std::wcout<<line.substr(start,pos-start)<<L"\n"; break; } cur++; start=pos+1; if(pos==line.size()) break; } } } } return 0; }

static int dispatch(const std::wstring& name, int argc, wchar_t **argv) {
    if(name==L"cat") return cat(argc,argv); if(name==L"echo") return echo_cmd(argc,argv); if(name==L"env") return env_cmd(argc,argv); if(name==L"printenv") return printenv_cmd(argc,argv); if(name==L"more") return more_less_cmd(argc,argv,L"more"); if(name==L"less") return more_less_cmd(argc,argv,L"less"); if(name==L"find") return find_cmd(argc,argv); if(name==L"tee") return tee_cmd(argc,argv); if(name==L"cut") return cut_cmd(argc,argv); if(name==L"pwd") return pwd(argc,argv); if(name==L"whoami") return whoami(argc,argv); if(name==L"hostname") return hostname(argc,argv); if(name==L"uname") return uname(argc,argv); if(name==L"clear") return clear(argc,argv); if(name==L"date") return date(argc,argv); if(name==L"touch") return touch(argc,argv); if(name==L"cp") return cp(argc,argv); if(name==L"mv") return mv(argc,argv); if(name==L"rm") return rm(argc,argv); if(name==L"rmdir") return rmdir_cmd(argc,argv); if(name==L"mkdir") return mkdir_cmd(argc,argv); if(name==L"wc") return wc_cmd(argc,argv); if(name==L"head") return head_tail(argc,argv,false); if(name==L"tail") return head_tail(argc,argv,true); if(name==L"grep") return grep(argc,argv); if(name==L"sort") return sort_cmd(argc,argv); if(name==L"uniq") return uniq(argc,argv); if(name==L"basename") return basename_cmd(argc,argv); if(name==L"dirname") return dirname_cmd(argc,argv); if(name==L"which") return which_cmd(argc,argv); if(name==L"sleep") return sleep_cmd(argc,argv); if(name==L"yes") return yes_cmd(argc,argv); if(name==L"true") return true_cmd(argc,argv); if(name==L"false") return false_cmd(argc,argv);
    std::wcerr << L"unknown Wsh core utility: " << name << L"\n"; return 127;
}

int wmain(int argc, wchar_t **argv) {
    std::wstring name = argc > 0 ? base_name(argv[0]) : L"coreutils";
    std::string narrow(name.begin(), name.end());
    return run_tool_logged(narrow.c_str(), [&]() { return dispatch(name, argc, argv); });
}
