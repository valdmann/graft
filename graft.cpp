#include <experimental/source_location>
#include <filesystem>
#include <fmt/format.h>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/mount.h>
#include <unistd.h>
#include <vector>

using namespace std;
using namespace std::experimental;
using namespace std::filesystem;
using namespace std::string_literals;

class error {
public:
    explicit error(int exit_code = EXIT_FAILURE, source_location location = source_location::current())
        : m_exit_code(exit_code)
    {
        cerr << fmt::format("{}:{}:({}): error",
                            location.file_name(),
                            location.line(),
                            location.function_name());
    }

    void operator<<(string_view string) && {
        cerr << ": " << string;
    }

    void operator<<(int e) && {
        cerr << ": " << strerror(e);
    }

    [[noreturn]]
    ~error()
    {
        cerr << endl;
        exit(m_exit_code);
    }

private:
    int m_exit_code;
};

vector<tuple<path, path>> parse_dot_graft(path p) {
    auto cwd = current_path();
    current_path(p);
    ifstream ifs(p / ".graft");
    vector<tuple<path, path>> ps;
    for (;;) {
        string dst;
        getline(ifs, dst, ifs.widen(':'));
        if (!ifs.good())
            break;
        string src;
        getline(ifs, src);
        ps.emplace_back(canonical(move(src)), canonical(move(dst)));
    }
    current_path(cwd);
    return ps;
}

vector<tuple<path, path>> read_dot_graft(path p) {
    for (; p.has_relative_path(); p = p.parent_path()) {
        if (auto ps = parse_dot_graft(p); !ps.empty())
            return move(ps);
    }
    error() << ".graft not found";
}

void unshare_mount_namespace() {
    if (unshare(CLONE_NEWNS) < 0)
        error() << errno;
}

void bind_mount(const path &src, const path &dst) {
    if (mount(src.c_str(), dst.c_str(), nullptr, MS_BIND | MS_REC, nullptr) < 0)
        error() << errno;
}

void unshare_root() {
    if (mount("none", "/", nullptr, MS_PRIVATE | MS_REC, nullptr) < 0)
        error() << errno;
}

void exec(const vector<string> &command) {
    vector<const char *> cs;
    for (const auto &s : command)
        cs.push_back(s.c_str());
    cs.push_back(nullptr);
    execvp(cs[0], const_cast<char**>(&cs[0]));
    error() << errno;
}

string_view get_shell() {
    if (auto p = getenv("SHELL"))
        return p;
    error() << "no $SHELL";
}

void set_environment_variable(const string_view &key, const string_view &value) {
    if (setenv(key.data(), value.data(), /* overwrite: */ true) < 0)
        error() << errno;
}

path append_many(path p, path::iterator begin, const path::iterator end)
{
    while (begin != end)
        p /= *begin++;
    return p;
}

int main(int ac, char **av) {
    auto cwd = canonical(current_path());
    const auto ps = read_dot_graft(cwd);
    unshare_mount_namespace();
    unshare_root();
    auto new_cwd = cwd;
    for (auto &[src, dst] : ps) {
        auto [src_it, cwd_it] = mismatch(src.begin(), src.end(), cwd.begin(), cwd.end());
        if (src_it == src.end())
            new_cwd = append_many(dst, cwd_it, cwd.end());
        bind_mount(src, dst);
    }
    set_environment_variable("OLDPWD", cwd.c_str());
    current_path(new_cwd);
    set_environment_variable("PWD", new_cwd.c_str());
    set_environment_variable("LD_LIBRARY_PATH", (path(getenv("HOME")) / "lib").c_str());
    vector<string> command(av+1, av+ac);
    if (command.empty())
        command.emplace_back(get_shell());
    seteuid(getuid());
    exec(command);
    return 0;
}
