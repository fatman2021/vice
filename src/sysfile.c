/*
 * sysfile.c - Simple locator for VICE system files.
 *
 * Written by
 *  Ettore Perazzoli <ettore@comm2000.it>
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See README for copyright notice.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA.
 *
 */

#include "vice.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "archdep.h"
#include "cmdline.h"
#include "embedded.h"
#include "findpath.h"
#include "ioutil.h"
#include "lib.h"
#include "log.h"
#include "resources.h"
#include "sysfile.h"
#include "translate.h"
#include "util.h"
#include "machine.h"

/* #define DBGSYSFILE */

#ifdef DBGSYSFILE
#define DBG(x)  printf x
#else
#define DBG(x)
#endif

/* Resources.  */

static char *default_path = NULL;
static char *system_path = NULL;
static char *expanded_system_path = NULL;

typedef struct alternate_name_s {
    const char *machine_name;
    const char *standard_name;
    const char *alternate_name;
} alternate_name_t;

static const alternate_name_t alternate_names[] = {
	{ "C64", "basic", "c-64-basic.rom" },
    { "C64", "chargen", "c-64-chargen.rom" },
    { "C64", "chargjp", "c-64-chargen-jp.rom" },
    { "C64", "kernal", "c-64-kernal.rom" },
    { "C64", "kernaljp", "c-64-kernal-jp.rom" },
    { "C64DTV", "basic", "c-64dtv-basic.rom" },
    { "C64DTV", "chargen", "c-64dtv-chargen.rom" },
    { "C64DTV", "dtvrom.bin", "c-64dtv-ext.rom" },
    { "C64DTV", "kernal", "c-64dtv-kernal.rom" },
    { "C128", "basic64", "c-128-basic-64.rom" },
    { "C128", "basichi", "c-128-basic-hi.rom" },
    { "C128", "basiclo", "c-128-basic-lo.rom" },
    { "C128", "chargch", "c-128-chargen-ch.rom" },
    { "C128", "chargde", "c-128-chargen-de.rom" },
    { "C128", "chargen", "c-128-chargen.rom" },
    { "C128", "chargfr", "c-128-chargen-fr.rom" },
    { "C128", "chargse", "c-128-chargen-se.rom" },
    { "C128", "chargno", "c-128-chargen-no.rom" },
    { "C128", "kernal", "c-128-kernal.rom" },
    { "C128", "kernal64", "c-128-kernal-64.rom" },
    { "C128", "kernalch", "c-128-kernal-ch.rom" },
    { "C128", "kernalde", "c-128-kernal-de.rom" },
    { "C128", "kernalfi", "c-128-kernal-fi.rom" },
    { "C128", "kernalfr", "c-128-kernal-fr.rom" },
    { "C128", "kernalit", "c-128-kernal-it.rom" },
    { "C128", "kernalno", "c-128-kernal-no.rom" },
    { "C128", "kernalse", "c-128-kernal-se.rom" },
    { "CBM-II", "basic.128", "c-600-128k-basic.rom" },
    { "CBM-II", "basic.128", "c-700-128k-basic.rom" },
    { "CBM-II", "basic.256", "c-600-256k-basic.rom" },
    { "CBM-II", "basic.256", "c-700-256k-basic.rom" },
    { "CBM-II", "basic.500", "c-500-basic.rom" },
    { "CBM-II", "chargen.500", "c-500-chargen.rom" },
    { "CBM-II", "chargen.600", "c-600-chargen.rom" },
    { "CBM-II", "chargen.700", "c-700-chargen.rom" },
    { "CBM-II", "kernal", "c-600-kernal.rom" },
    { "CBM-II", "kernal", "c-700-kernal.rom" },
    { "CBM-II", "kernal.500", "c-500-kernal.rom" },
    { "PET", "basic1", "c-2000-basic.rom" },
    { "PET", "basic2", "c-3000-basic.rom" },
    { "PET", "basic4", "c-4000-basic.rom" },
    { "PET", "basic4", "c-8000-basic.rom" },
    { "PET", "characters.901640-01.bin", "c-9000-chargen.rom" },
    { "PET", "chargen", "c-2000-chargen.rom" },
    { "PET", "chargen", "c-3000-chargen.rom" },
    { "PET", "chargen", "c-4000-chargen.rom" },
    { "PET", "chargen", "c-8000-chargen.rom" },
    { "PET", "chargen.de", "c-pet-chargen-de.rom" },
    { "PET", "edit1g", "c-2000-editor.rom" },
    { "PET", "edit2b", "c-3000-b-editor.rom" },
    { "PET", "edit2g", "c-3000-g-editor.rom" },
    { "PET", "edit4b40", "c-4000-b-editor.rom" },
    { "PET", "edit4b80", "c-8000-editor.rom" },
    { "PET", "edit4g40", "c-4000-g-editor.rom" },
    { "PET", "kernal1", "c-2000-kernal.rom" },
    { "PET", "kernal2", "c-3000-kernal.rom" },
    { "PET", "kernal4", "c-4000-kernal.rom" },
    { "PET", "kernal4", "c-8000-kernal.rom" },
    { "PET", "waterloo-a000.901898-01.bin", "c-9000-ext-a.rom" },
    { "PET", "waterloo-b000.901898-02.bin", "c-9000-ext-b.rom" },
    { "PET", "waterloo-c000.901898-03.bin", "c-9000-ext-c.rom" },
    { "PET", "waterloo-d000.901898-04.bin", "c-9000-ext-d.rom" },
    { "PET", "waterloo-e000.901897-01.bin", "c-9000-ext-e.rom" },
    { "PET", "waterloo-f000.901898-05.bin", "c-9000-ext-f.rom" },
    { "PLUS4", "3plus1hi", "c-plus4-ext-hi.rom" },
    { "PLUS4", "3plus1lo", "c-plus4-ext-lo.rom" },
    { "PLUS4", "basic", "c-plus4-basic.rom" },
    { "PLUS4", "basic", "c-16-basic.rom" },
    { "PLUS4", "c2lo.364", "c-v364-speech.rom" },
    { "PLUS4", "kernal", "c-plus4-kernal.rom" },
    { "PLUS4", "kernal", "c-16-kernal.rom" },
    { "PLUS4", "kernal.232", "c-232-kernal.rom" },
    { "PLUS4", "kernal.364", "c-v364-kernal.rom" },
    { "VIC20", "basic", "c-vic20-basic.rom" },
    { "VIC20", "chargen", "c-vic20-chargen.rom" },
    { "VIC20", "chargjp", "c-vic20-chargen-jp.rom" },
    { "VIC20", "kernal", "c-vic20-kernal.rom" },
    { "VIC20", "kernaljp", "c-vic20-kernal-jp.rom" },
    { NULL, "cbm1526", "c-printer-mps802.rom" },
    { NULL, "d1541II", "c-drive-1541-ii.rom" },
    { NULL, "d1571cr", "c-drive-1571-c128dcr.rom" },
    { NULL, "dos1001", "c-drive-1001.rom" },
    { NULL, "dos1001", "c-drive-8050.rom" },
    { NULL, "dos1001", "c-drive-8250.rom" },
    { NULL, "dos1541", "c-drive-1541.rom" },
    { NULL, "dos1551", "c-drive-1551.rom" },
    { NULL, "dos1570", "c-drive-1570.rom" },
    { NULL, "dos1571", "c-drive-1571.rom" },
    { NULL, "dos1581", "c-drive-1581.rom" },
    { NULL, "dos2000", "c-drive-2000.rom" },
    { NULL, "dos2031", "c-drive-2031.rom" },
    { NULL, "dos2040", "c-drive-2040.rom" },
    { NULL, "dos3040", "c-drive-3040.rom" },
    { NULL, "dos4000", "c-drive-4000.rom" },
    { NULL, "dos4040", "c-drive-4040.rom" },
    { NULL, "mps801", "c-printer-mps801.rom" },
    { NULL, "mps803", "c-printer-mps803.rom" },
    { NULL, "nl10-cbm", "star-printer-nl10-c.rom" }
};

static const char *get_alternate_name(const char *standard_name)
{
    const alternate_name_t *an = alternate_names;
    int count = sizeof(alternate_names) / sizeof(alternate_name_t);

    for (; count > 0; count--, an++) {
        if ((an->machine_name == NULL || strcmp(an->machine_name, machine_name) == 0) &&
            strcmp(an->standard_name, standard_name) == 0) {
            return an->alternate_name;
        }
    }
    return standard_name;
}

static int set_system_path(const char *val, void *param)
{
    char *tmp_path, *tmp_path_save, *p, *s, *current_dir;

    util_string_set(&system_path, val);

    lib_free(expanded_system_path);
    expanded_system_path = NULL; /* will subsequently be replaced */

    tmp_path_save = util_subst(system_path, "$$", default_path); /* malloc'd */

    current_dir = ioutil_current_dir();

    tmp_path = tmp_path_save; /* tmp_path points into tmp_path_save */
    do {
        p = strstr(tmp_path, ARCHDEP_FINDPATH_SEPARATOR_STRING);

        if (p != NULL) {
            *p = 0;
        }
        if (!archdep_path_is_relative(tmp_path)) {
            /* absolute path */
            if (expanded_system_path == NULL) {
                s = util_concat(tmp_path, NULL); /* concat allocs a new str. */
            } else {
                s = util_concat(expanded_system_path,
                                ARCHDEP_FINDPATH_SEPARATOR_STRING,
                                tmp_path, NULL );
            }
        } else { /* relative path */
            if (expanded_system_path == NULL) {
                s = util_concat(current_dir,
                                FSDEV_DIR_SEP_STR,
                                tmp_path, NULL );
            } else {
                s = util_concat(expanded_system_path,
                                ARCHDEP_FINDPATH_SEPARATOR_STRING,
                                current_dir,
                                FSDEV_DIR_SEP_STR,
                                tmp_path, NULL );
            }
        }
        lib_free(expanded_system_path);
        expanded_system_path = s;

        tmp_path = p + strlen(ARCHDEP_FINDPATH_SEPARATOR_STRING);
    } while (p != NULL);

    lib_free(current_dir);
    lib_free(tmp_path_save);

    DBG(("set_system_path -> expanded_system_path:'%s'\n", expanded_system_path));

    return 0;
}

static const resource_string_t resources_string[] = {
    { "Directory", "$$", RES_EVENT_NO, NULL,
      &system_path, set_system_path, NULL },
    RESOURCE_STRING_LIST_END
};

/* Command-line options.  */

static const cmdline_option_t cmdline_options[] = {
    { "-directory", SET_RESOURCE, 1,
      NULL, NULL, "Directory", NULL,
      USE_PARAM_ID, USE_DESCRIPTION_ID,
      IDCLS_P_PATH, IDCLS_DEFINE_SYSTEM_FILES_PATH,
      NULL, NULL },
    CMDLINE_LIST_END
};

/* ------------------------------------------------------------------------- */

int sysfile_init(const char *emu_id)
{
    default_path = archdep_default_sysfile_pathlist(emu_id);
    DBG(("sysfile_init(%s) -> default_path:'%s'\n", emu_id, default_path));
    /* HACK: set the default value early, so the systemfile locater also works
             in early startup */
    set_system_path("$$", NULL);
    return 0;
}

void sysfile_shutdown(void)
{
    lib_free(default_path);
    lib_free(expanded_system_path);
}

int sysfile_resources_init(void)
{
    return resources_register_string(resources_string);
}

void sysfile_resources_shutdown(void)
{
    lib_free(system_path);
}

int sysfile_cmdline_options_init(void)
{
    return cmdline_register_options(cmdline_options);
}

/* Locate a system file called `name' by using the search path in
   `Directory', checking that the file can be accesses in mode `mode', and
   return an open stdio stream for that file.  If `complete_path_return' is
   not NULL, `*complete_path_return' points to a malloced string with the
   complete path if the file was found or is NULL if not.  */
FILE *sysfile_open(const char *name, char **complete_path_return,
                   const char *open_mode)
{
    char *p = NULL;
    FILE *f;

    if (name == NULL || *name == '\0') {
        log_error(LOG_DEFAULT, "Missing name for system file.");
        return NULL;
    }

    p = findpath(name, expanded_system_path, IOUTIL_ACCESS_R_OK);

    if (p == NULL) {
        if (complete_path_return != NULL) {
            *complete_path_return = NULL;
        }
        return NULL;
    } else {
        f = fopen(p, open_mode);

        if (f == NULL || complete_path_return == NULL) {
            lib_free(p);
            p = NULL;
        }
        if (complete_path_return != NULL) {
            *complete_path_return = p;
        }
        return f;
    }
}

/* As `sysfile_open', but do not open the file.  Just return 0 if the file is
   found and is readable, or -1 if an error occurs.  */
int sysfile_locate(const char *name, char **complete_path_return)
{
    FILE *f = sysfile_open(name, complete_path_return, MODE_READ);

    if (f != NULL) {
        fclose(f);
        return 0;
    } else {
        return -1;
    }
}

/* ------------------------------------------------------------------------- */

/*
 * If minsize >= 0, and the file is smaller than maxsize, load the data
 * into the end of the memory range.
 * If minsize < 0, load it at the start.
 */
int sysfile_load(const char *name, BYTE *dest, int minsize, int maxsize)
{
    FILE *fp = NULL;
    size_t rsize = 0;
    char *complete_path = NULL;
    int load_at_end;


/*
 * This feature is only active when --enable-embedded is given to the
 * configure script, its main use is to make developing new ports easier
 * and to allow ports for platforms which don't have a filesystem, or a
 * filesystem which is hard/impossible to load data files from.
 *
 * when USE_EMBEDDED is defined this will check if a
 * default system file is loaded, when USE_EMBEDDED
 * is not defined the function is just 0 and will
 * be optimized away.
 */

    if ((rsize = embedded_check_file(name, dest, minsize, maxsize)) != 0) {
        return rsize;
    }

    fp = sysfile_open(get_alternate_name(name), &complete_path, MODE_READ);

    if (fp == NULL) {
        fp = sysfile_open(name, &complete_path, MODE_READ);
    }

    if (fp == NULL) {
        /* Try to open the file from the current directory. */
        const char working_dir_prefix[3] = {
            '.', FSDEV_DIR_SEP_CHR, '\0'
        };
        char *local_name = NULL;

        local_name = util_concat(working_dir_prefix, name, NULL);
        fp = sysfile_open((const char *)local_name, &complete_path, MODE_READ);
        if (fp == NULL) {
			log_verbose("System file load attempt failed: \"%s\", \"%s\", \"%s\". System path: \"%s\".", get_alternate_name(name), name, local_name, expanded_system_path);
        }
        lib_free(local_name);
        local_name = NULL;

        if (fp == NULL) {
            goto fail;
        }
    }

    log_message(LOG_DEFAULT, "Loading system file `%s'.", complete_path);

    rsize = util_file_length(fp);
    if (minsize < 0) {
        minsize = -minsize;
        load_at_end = 0;
    } else {
        load_at_end = 1;
    }

    if (rsize < ((size_t)minsize)) {
        log_error(LOG_DEFAULT, "ROM %s: short file.", complete_path);
        goto fail;
    }
    if (rsize == ((size_t)maxsize + 2)) {
        log_warning(LOG_DEFAULT,
                    "ROM `%s': two bytes too large - removing assumed "
                    "start address.", complete_path);
        if (fread((char *)dest, 1, 2, fp) < 2) {
            goto fail;
        }
        rsize -= 2;
    }
    if (load_at_end && rsize < ((size_t)maxsize)) {
        dest += maxsize - rsize;
    } else if (rsize > ((size_t)maxsize)) {
        log_warning(LOG_DEFAULT, "ROM `%s': long file, discarding end.",
                    complete_path);
        rsize = maxsize;
    }
    if ((rsize = fread((char *)dest, 1, rsize, fp)) < ((size_t)minsize)) {
        goto fail;
    }

    fclose(fp);
    lib_free(complete_path);
    return (int)rsize;  /* return ok */

fail:
    lib_free(complete_path);
    return -1;
}