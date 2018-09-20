#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/mount.h>
#include <unistd.h>
#include <vector>

using namespace std;
using namespace std::filesystem;
using namespace std::string_literals;

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
    throw runtime_error(".graft not found");
}

void unshare_mount_namespace() {
    if (unshare(CLONE_NEWNS) < 0)
        throw system_error(errno, generic_category());
}

void bind_mount(const path &src, const path &dst) {
    if (mount(src.c_str(), dst.c_str(), nullptr, MS_BIND | MS_REC, nullptr) < 0)
        throw system_error(errno, generic_category());
}

void unshare_root() {
    if (mount("none", "/", nullptr, MS_PRIVATE | MS_REC, nullptr) < 0)
        throw system_error(errno, generic_category());
}

void exec(const vector<string> &command) {
    vector<const char *> cs;
    for (const auto &s : command)
        cs.push_back(s.c_str());
    cs.push_back(nullptr);
    execvp(cs[0], const_cast<char**>(&cs[0]));
    throw system_error(errno, generic_category());
}

string_view get_shell() {
    if (auto p = getenv("SHELL"))
        return p;
    throw runtime_error("no SHELL");
}

void set_environment_variable(const string_view &key, const string_view &value) {
    if (setenv(key.data(), value.data(), /* overwrite: */ true) < 0)
        throw system_error(errno, generic_category());
}

path append_many(path p, path::iterator begin, const path::iterator end)
{
    while (begin != end)
        p /= *begin++;
    return p;
}

int main(int ac, char **av) {
    try {
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
        vector<string> command(av+1, av+ac);
        if (command.empty())
            command.emplace_back(get_shell());
        seteuid(getuid());
        exec(command);
        return 0;
    } catch (const exception &e) {
        cerr << e.what() << "\n";
        return 1;
    }
}
