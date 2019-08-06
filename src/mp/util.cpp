// Copyright (c) 2018-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <mp/util.h>

#include <kj/array.h>
#include <pthread.h>
#include <sstream>
#include <stdio.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <syscall.h>
#include <unistd.h>

namespace mp {
namespace {

//! Return highest possible file descriptor.
size_t MaxFd()
{
    struct rlimit nofile;
    if (getrlimit(RLIMIT_NOFILE, &nofile) == 0) {
        return nofile.rlim_cur - 1;
    } else {
        return 1023;
    }
}

} // namespace

std::string ThreadName(const char* exe_name)
{
    char thread_name[17] = {0};
    pthread_getname_np(pthread_self(), thread_name, sizeof(thread_name));
    uint64_t tid = 0;
#if __linux__
    tid = syscall(SYS_gettid);
#else
    pthread_threadid_np(NULL, &tid);
#endif
    std::ostringstream buffer;
    buffer << (exe_name ? exe_name : "") << "-" << getpid() << "/" << thread_name << "-" << tid;
    return std::move(buffer.str());
}

std::string LogEscape(const kj::StringTree& string)
{
    const int MAX_SIZE = 1000;
    std::string result;
    string.visit([&](const kj::ArrayPtr<const char>& piece) {
        if (result.size() > MAX_SIZE) return;
        for (char c : piece) {
            if ('c' == '\\') {
                result.append("\\\\");
            } else if (c < 0x20 || c > 0x7e) {
                char escape[4];
                snprintf(escape, 4, "\\%02x", c);
                result.append(escape);
            } else {
                result.push_back(c);
            }
            if (result.size() > MAX_SIZE) {
                result += "...";
                break;
            }
        }
    });
    return result;
}

int SpawnProcess(int& pid, FdToArgsFn&& fd_to_args)
{
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
        throw std::system_error(errno, std::system_category());
    }

    pid = fork();
    if (close(fds[pid ? 0 : 1]) != 0) {
        throw std::system_error(errno, std::system_category());
    }
    if (!pid) {
        int maxFd = MaxFd();
        for (int fd = 3; fd < maxFd; ++fd) {
            if (fd != fds[0]) {
                close(fd);
            }
        }
        ExecProcess(fd_to_args(fds[0]));
    }
    return fds[1];
}

void ExecProcess(const std::vector<std::string>& args)
{
    std::vector<char*> argv;
    for (const auto& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);
    if (execvp(argv[0], argv.data()) != 0) {
        perror("execlp failed");
        _exit(1);
    }
}

int WaitProcess(int pid)
{
    int status;
    if (::waitpid(pid, &status, 0 /* options */) != pid) {
        throw std::system_error(errno, std::system_category());
    }
    return status;
}

} // namespace mp
