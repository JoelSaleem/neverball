/*
 * Copyright (C) 2003 Robert Kooima
 *
 * NEVERBALL is  free software; you can redistribute  it and/or modify
 * it under the  terms of the GNU General  Public License as published
 * by the Free  Software Foundation; either version 2  of the License,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT  ANY  WARRANTY;  without   even  the  implied  warranty  of
 * MERCHANTABILITY or  FITNESS FOR A PARTICULAR PURPOSE.   See the GNU
 * General Public License for more details.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "base_config.h"
#include "common.h"
#include "log.h"
#include "fs.h"

#ifdef _WIN32
#include <shlobj.h>
#endif

#ifdef __ANDROID__
#include <SDL.h>
#include <sys/stat.h>
#endif

/*---------------------------------------------------------------------------*/

#ifdef __ANDROID__
/*
 * The APK ships the game data as one zip asset. Copy it out to internal
 * storage whenever its size changes, so the archive scanner can mount it.
 */
static const char *android_data_path(void)
{
    static char dir[MAXSTR];
    char dst[MAXSTR];
    struct stat st;
    SDL_RWops *src;
    FILE *out;

    SAFECPY(dir, SDL_AndroidGetInternalStoragePath());
    SAFECAT(dir, "/data");
    mkdir(dir, 0700);

    SAFECPY(dst, dir);
    SAFECAT(dst, "/neverball-data.zip");

    if (!(src = SDL_RWFromFile("neverball-data.zip", "rb")))
    {
        log_printf("Missing data asset: %s\n", SDL_GetError());
        return dir;
    }

    if (stat(dst, &st) != 0 || st.st_size != SDL_RWsize(src))
    {
        static char buf[1 << 20];
        size_t n;
        int ok = 1;

        log_printf("Unpacking game data to %s\n", dst);

        if ((out = fopen(dst, "wb")))
        {
            while (ok && (n = SDL_RWread(src, buf, 1, sizeof (buf))) > 0)
                ok = (fwrite(buf, 1, n, out) == n);

            if (fclose(out) != 0 || !ok)
            {
                log_printf("Failure to unpack game data\n");
                remove(dst);
            }
        }
        else log_printf("Failure to open %s\n", dst);
    }

    SDL_RWclose(src);

    return dir;
}
#endif

static const char *pick_data_path(const char *arg_data_path)
{
#ifdef __EMSCRIPTEN__
    return "/data";
#elif defined(__ANDROID__)
    return android_data_path();
#else
    static char dir[MAXSTR];
    char *env;

    if (arg_data_path)
        return arg_data_path;

    if ((env = getenv("NEVERBALL_DATA")))
        return env;

    if (path_is_abs(CONFIG_DATA))
        return CONFIG_DATA;

    SAFECPY(dir, fs_base_dir());
    SAFECAT(dir, "/");
    SAFECAT(dir, CONFIG_DATA);

    return dir;
#endif
}

static const char *pick_home_path(void)
{
#ifdef _WIN32
    static char path[MAX_PATH];

    if (SHGetFolderPath(NULL, CSIDL_PERSONAL, NULL, 0, path) == S_OK)
    {
        static char gamepath[MAX_PATH];

        SAFECPY(gamepath, path);
        SAFECAT(gamepath, "\\My Games");

        if (dir_exists(gamepath) || dir_make(gamepath) == 0)
            return gamepath;

        return path;
    }
    else
        return fs_base_dir();
#elif defined(__ANDROID__)
    return SDL_AndroidGetInternalStoragePath();
#else
    const char *path;

    return (path = getenv("HOME")) ? path : fs_base_dir();
#endif
}

void config_paths(const char *arg_data_path)
{
    const char *data, *home, *user;

    /*
     * Scan in turn the game data and user directories for archives,
     * adding each archive to the search path.  Archives with names
     * further down the alphabet take precedence.  After each scan,
     * add the directory itself, taking precedence over archives added
     * so far.
     */

    /* Data directory. */

    data = pick_data_path(arg_data_path);

    fs_add_path_with_archives(data);

    /* User directory. */

    home = pick_home_path();

#ifdef __EMSCRIPTEN__
    /* Force IndexedDB-backed location created during Module['preRun']. */
    user = strdup("/neverball");
#else
    user = concat_string(home, "/", CONFIG_USER, NULL);
#endif

    /* Set up directory for writing, create if needed. */

    if (!fs_set_write_dir(user))
    {
        int success = 0;

        log_printf("Failure to establish write directory. First run?\n");

        if (fs_set_write_dir(home))
            if (fs_mkdir(CONFIG_USER))
                if (fs_set_write_dir(user))
                    success = 1;

        if (success)
        {
            log_printf("Write directory established at %s\n", user);
        }
        else
        {
            log_printf("Write directory not established at %s\n", user);
            fs_set_write_dir(NULL);
        }
    }

    fs_add_path_with_archives(user);

    free((void *) user);
}

/*---------------------------------------------------------------------------*/
