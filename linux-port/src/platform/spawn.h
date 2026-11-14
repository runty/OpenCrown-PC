#pragma once
#include <spawn.h>

namespace dcp::platform {
inline int spawn_chdir(posix_spawn_file_actions_t* actions, const char* path) {
#ifdef __APPLE__
    return posix_spawn_file_actions_addchdir(actions, path);
#else
    return posix_spawn_file_actions_addchdir_np(actions, path);
#endif
}
inline int close_spawn_descriptors(posix_spawn_file_actions_t* actions) {
#ifdef __APPLE__
    // spawn() applies CLOEXEC_DEFAULT, while file actions preserve explicit stdio.
    (void)actions;
    return 0;
#else
    return posix_spawn_file_actions_addclosefrom_np(actions, 3);
#endif
}
inline int spawn(pid_t* pid, const char* path, const posix_spawn_file_actions_t* actions,
                 char* const argv[], char* const environment[], bool search = false) {
#ifdef __APPLE__
    posix_spawnattr_t attributes;
    int result = posix_spawnattr_init(&attributes);
    if (result != 0) return result;
    result = posix_spawnattr_setflags(&attributes, POSIX_SPAWN_CLOEXEC_DEFAULT);
    if (result == 0)
        result = search ? posix_spawnp(pid, path, actions, &attributes, argv, environment)
                        : posix_spawn(pid, path, actions, &attributes, argv, environment);
    posix_spawnattr_destroy(&attributes);
    return result;
#else
    return search ? posix_spawnp(pid, path, actions, nullptr, argv, environment)
                  : posix_spawn(pid, path, actions, nullptr, argv, environment);
#endif
}
}
