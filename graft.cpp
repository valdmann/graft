#include <experimental/filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/mount.h>
#include <unistd.h>
#include <vector>

using namespace std;
using namespace std::experimental::filesystem;
using namespace std::string_literals;

string read_line(const path &p) {
    ifstream ifs(p);
    string contents;
    getline(ifs, contents);
    return contents;
}

tuple<path, path, path> read_dot_graft(path pre1) {
    for (auto suf = path(); !pre1.empty();) {
        if (auto pre2 = read_line(pre1 / ".graft"); !pre2.empty()) {
            return tuple(move(pre1), move(pre2), move(suf));
        }
        suf = pre1.filename() / suf;
        pre1 = pre1.parent_path();
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

int main(int ac, char **av) {
    try {
        auto cwd = current_path();
        const auto [pre1, pre2, suf] = read_dot_graft(cwd);
        unshare_mount_namespace();
        unshare_root();
        bind_mount(pre1, pre2);
        set_environment_variable("GRAFT", (pre1.string() + ':' + pre2.string()).c_str());
        set_environment_variable("OLDPWD", cwd.c_str());
        current_path(cwd = pre2 / suf);
        set_environment_variable("PWD", cwd.c_str());
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
