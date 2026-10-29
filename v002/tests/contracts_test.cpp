// Death-test for jText's contracts: calling the writer out of order must terminate
// (native contract_assert on gcc16 v002; the runtime check elsewhere), and the right
// order must not.

#include <jText.h>

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace jText::v002;

// Run `fn` in a forked child; true iff the child did not exit cleanly (a contract fired).
template <class Fn>
static bool child_terminates(Fn fn) {
    std::fflush(nullptr);
    const pid_t pid = fork();
    if (pid < 0) { std::perror("fork"); std::_Exit(2); }
    if (pid == 0) {
        fn();
        std::_Exit(0);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) { /* retry on EINTR */ }
    return !(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

int main() {
    int failures = 0;
    auto expect = [&](bool terminates, const char* what, auto fn) {
        if (child_terminates(fn) != terminates) {
            std::cerr << "FAIL: " << what << (terminates ? " did NOT terminate\n" : " terminated\n");
            ++failures;
        }
    };

    expect(false, "a Full file written in order", [] {
        std::ostringstream os;
        JTextWriter w(os);
        w.set_profile(JTextProfile::Full);
        w.set_header_comment(3, "why");
        w.write_header();
        w.begin_section("T");
        w.begin_fields();
        w.end_fields();
        w.begin_data();
        w.append_row(1, {"1", "a"});
        w.finalize();
    });
    expect(true, "set_purpose after the header", [] {
        std::ostringstream os;
        JTextWriter w(os);
        w.write_header();
        w.set_purpose("late");
    });
    expect(true, "set_profile after the header", [] {
        std::ostringstream os;
        JTextWriter w(os);
        w.write_header();
        w.set_profile(JTextProfile::Full);
    });
    expect(true, "set_header_comment on line 9", [] {
        std::ostringstream os;
        JTextWriter w(os);
        w.set_header_comment(9, "no such line");
    });
    expect(true, "begin_data in a Light file", [] {
        std::ostringstream os;
        JTextWriter w(os);
        w.begin_section("T");
        w.begin_data();
    });
    expect(true, "begin_fields outside a section", [] {
        std::ostringstream os;
        JTextWriter w(os);
        w.set_profile(JTextProfile::Full);
        w.begin_fields();
    });
    expect(true, "write_template after the Data block", [] {
        std::ostringstream os;
        JTextWriter w(os);
        w.set_profile(JTextProfile::Full);
        w.begin_section("T");
        w.begin_data();
        w.write_template("t", "body");
    });

    if (failures == 0) std::cout << "jText contracts — writer misuse terminates, the right order does not\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
