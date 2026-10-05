#include "addons/addon_id_safety.hpp"
#include "addons/zip_safety.hpp"
#ifndef _WIN32
#include "online/url_launcher.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <fstream>
#include <iostream>
#include <string>

static int failures = 0;
static void check(bool condition, const char* description)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << description << "\n";
        ++failures;
    }
}

int main(int argc, char** argv)
{
#ifndef _WIN32
    // launchURL re-enters this executable with the URL as its sole argument.
    if (argc == 2)
    {
        const char* capture = getenv("MK_URL_CAPTURE");
        if (!capture) return 2;
        std::ofstream out(capture, std::ios::binary);
        out << argv[1];
        return out ? 0 : 3;
    }
#endif
    check(AddonIdSafety::isValid("winter_track-2"), "accept normal add-on id");
    check(!AddonIdSafety::isValid("../outside"), "reject traversal add-on id");
    check(!AddonIdSafety::isValid("name/subdir"), "reject slash in add-on id");
    check(!AddonIdSafety::isValid("name. "), "reject punctuation in add-on id");
    check(!AddonIdSafety::isValid(std::string(129, 'a')), "bound add-on id length");

    check(ZipSafety::isSafeName("tracks/winter/track.xml"), "accept nested archive name");
    check(!ZipSafety::isSafeName("../outside"), "reject dot-dot traversal");
    check(!ZipSafety::isSafeName("tracks/../../outside"), "reject nested traversal");
    check(!ZipSafety::isSafeName("/absolute"), "reject absolute archive name");
    check(!ZipSafety::isSafeName("C:/outside"), "reject drive path");
    check(!ZipSafety::isSafeName("folder\\outside"), "reject backslash path");
    check(!ZipSafety::isSafeName("folder//outside"), "reject empty path component");
#ifdef _WIN32
    check(!ZipSafety::isSafeName("tracks/NUL.txt"), "reject Windows device filename");
#endif
    check(!ZipSafety::isSafeName("tracks/trailing. "), "reject Windows path normalization suffix");

    check(ZipSafety::withinBudget(10000, 1024, ZipSafety::ADDON_LIMITS),
          "accept add-on entry and byte boundary");
    check(!ZipSafety::withinBudget(10001, 1024, ZipSafety::ADDON_LIMITS),
          "reject too many add-on entries");
    check(!ZipSafety::canAddFile(ZipSafety::ADDON_LIMITS.expanded_bytes - 1,
                                 2, ZipSafety::ADDON_LIMITS),
          "reject aggregate expanded-byte overflow");
    check(!ZipSafety::canAddFile(0, ZipSafety::ADDON_LIMITS.file_bytes + 1,
                                 ZipSafety::ADDON_LIMITS),
          "reject oversized individual file");
    check(ZipSafety::canAddFile(0, 1ULL * 1024 * 1024 * 1024,
                                ZipSafety::MOBILE_ASSET_LIMITS),
          "allow large mobile asset file");

#ifndef _WIN32
    char temp_template[] = "/tmp/mk-security-test-XXXXXX";
    char* temp = mkdtemp(temp_template);
    check(temp != NULL, "create temporary test directory");
    if (temp)
    {
        const std::string root = temp;
        const std::string target = root + "/target";
        const std::string link_root = root + "/linked-root";
        const std::string link_child = root + "/target/link";
        mkdir(target.c_str(), 0700);
        check(ZipSafety::isConfinedPath(target, "safe/file.dat"),
              "confine safe path under target root");
        symlink(target.c_str(), link_root.c_str());
        check(!ZipSafety::isConfinedPath(link_root, "file.dat"),
              "reject symlink destination root");
        symlink(root.c_str(), link_child.c_str());
        check(!ZipSafety::isConfinedPath(target, "link/outside.dat"),
              "reject symlink in destination path");

        char exe_path[4096];
        check(realpath(argv[0], exe_path) != NULL, "resolve test executable path");
        const std::string capture = root + "/captured-url";
        setenv("MK_URL_CAPTURE", capture.c_str(), 1);
        const std::string payload = "https://example.test/;touch${IFS}" + root + "/injected";
        check(OnlineURL::isAllowedWebURL(payload), "accept web URL containing shell punctuation");
        check(OnlineURL::launchURL(exe_path, payload), "launch helper process successfully");
        std::ifstream in(capture.c_str(), std::ios::binary);
        const std::string actual((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
        check(actual == payload, "pass URL as one unchanged process argument");
        check(access((root + "/injected").c_str(), F_OK) != 0,
              "shell punctuation does not execute a command");
        check(!OnlineURL::isAllowedWebURL("javascript:alert(1)"),
              "reject non-web URL schemes");
        check(!OnlineURL::isAllowedWebURL("https:///missing-host"),
              "reject URL with missing host");
        check(!OnlineURL::isAllowedWebURL(std::string("https://host/\n")),
              "reject control characters in URL");
    }
#endif
    return failures ? 1 : 0;
}
