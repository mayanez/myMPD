/*
 SPDX-License-Identifier: GPL-3.0-or-later
 myMPD (c) 2018-2022 Juergen Mang <mail@jcgames.de>
 https://github.com/jcorporation/mympd
*/

#include "mympd_config_defs.h"
#include "filehandler.h"

#include "api.h"
#include "log.h"
#include "sds_extras.h"

#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <libgen.h>
#include <netdb.h>
#include <sys/socket.h>

/**
 * Getline function that trims whitespace characters
 * @param s an already allocated sds string
 * @param fp a file descriptor to read from
 * @param max max line length to read
 * @return GETLINE_OK on success,
 *         GETLINE_EMPTY for empty line,
 *         GETLINE_TOO_LONG for too long line
 */
int sds_getline(sds *s, FILE *fp, size_t max) {
    sdsclear(*s);
    size_t i = 0;
    for (;;) {
        int c = fgetc(fp);
        if (c == EOF) {
            sdstrim(*s, "\r \t");
            if (sdslen(*s) > 0) {
                return GETLINE_OK;
            }
            return GETLINE_EMPTY;
        }
        if (c == '\n') {
            sdstrim(*s, "\r \t");
            return GETLINE_OK;
        }
        if (i < max) {
            *s = sds_catchar(*s, (char)c);
            i++;
        }
        else {
            MYMPD_LOG_ERROR("Line is too long, max length is %lu", (unsigned long)max);
            return GETLINE_TOO_LONG;
        }
    }
}

/**
 * Getline function that first trims whitespace characters and adds a newline afterwards
 * @param s an already allocated sds string
 * @param fp a file descriptor to read from
 * @param max max line length to read
 * @return GETLINE_OK on success,
 *         GETLINE_EMPTY for empty line,
 *         GETLINE_TOO_LONG for too long line
 */
int sds_getline_n(sds *s, FILE *fp, size_t max) {
    int rc = sds_getline(s, fp, max);
    *s = sdscat(*s, "\n");
    return rc;
}

/**
 * Reads a whole file in the sds string s from *fp
 * @param s an already allocated sds string that should hold the file content
 * @param fp FILE pointer to read
 * @param max maximum bytes to read
 * @return GETLINE_OK on success,
 *         GETLINE_EMPTY for empty file,
 *         GETLINE_TOO_LONG for too long file
 */
int sds_getfile(sds *s, FILE *fp, size_t max) {
    sdsclear(*s);
    size_t i = 0;
    for (;;) {
        int c = fgetc(fp);
        if (c == EOF) {
            sdstrim(*s, "\r \t\n");
            MYMPD_LOG_DEBUG("Read %lu bytes from file", (unsigned long)sdslen(*s));
            if (sdslen(*s) > 0) {
                return GETLINE_OK;
            }
            return GETLINE_EMPTY;
        }
        if (i < max) {
            *s = sds_catchar(*s, (char)c);
            i++;
        }
        else {
            MYMPD_LOG_ERROR("File is too long, max length is %lu", (unsigned long)max);
            return GETLINE_TOO_LONG;
        }
    }
}

int testdir(const char *name, const char *dirname, bool create) {
    DIR* dir = opendir(dirname);
    if (dir != NULL) {
        closedir(dir);
        MYMPD_LOG_NOTICE("%s: \"%s\"", name, dirname);
        //directory exists
        return DIR_EXISTS;
    }

    if (create == true) {
        errno = 0;
        if (mkdir(dirname, 0770) != 0) {
            MYMPD_LOG_ERROR("%s: creating \"%s\" failed", name, dirname);
            MYMPD_LOG_ERRNO(errno);
            //directory does not exist and creating it failed
            return DIR_CREATE_FAILED;
        }
        MYMPD_LOG_NOTICE("%s: \"%s\" created", name, dirname);
        //directory successfully created
        return DIR_CREATED;
    }

    MYMPD_LOG_ERROR("%s: \"%s\" does not exist", name, dirname);
    //directory does not exist
    return DIR_NOT_EXISTS;
}

//opens tmp file for write
FILE *open_tmp_file(sds filepath) {
    errno = 0;
    int fd = mkstemp(filepath);
    if (fd < 0) {
        MYMPD_LOG_ERROR("Can not open file \"%s\" for write", filepath);
        MYMPD_LOG_ERRNO(errno);
        return NULL;
    }
    errno = 0;
    FILE *fp = fdopen(fd, "w");
    if (fp == NULL) {
        MYMPD_LOG_ERROR("Can not open file \"%s\" for write", filepath);
        MYMPD_LOG_ERRNO(errno);
    }
    return fp;
}

//removes a file and reports all errors
bool rm_file(sds filepath) {
    errno = 0;
    if (unlink(filepath) != 0) {
        MYMPD_LOG_ERROR("Error removing file \"%s\"", filepath);
        MYMPD_LOG_ERRNO(errno);
        return false;
    }
    return true;
}

//removes a file and ignores none existing error
int try_rm_file(sds filepath) {
    errno = 0;
    if (unlink(filepath) != 0) {
        if (errno == ENOENT) {
            MYMPD_LOG_DEBUG("File \"%s\" does not exist", filepath);
            return RM_FILE_ENOENT;
        }
        MYMPD_LOG_ERROR("Error removing file \"%s\"", filepath);
        MYMPD_LOG_ERRNO(errno);
        return RM_FILE_ERROR;
    }
    return RM_FILE_OK;
}

//closes the tmp file and moves it to its destination name
bool rename_tmp_file(FILE *fp, sds tmp_file, sds filepath, bool write_rc) {
    if (fclose(fp) != 0 ||
        write_rc == false)
    {
        MYMPD_LOG_ERROR("Error writing data to file \"%s\"", tmp_file);
        rm_file(tmp_file);
        FREE_SDS(tmp_file);
        return false;
    }
    errno = 0;
    if (rename(tmp_file, filepath) == -1) {
        MYMPD_LOG_ERROR("Rename file from \"%s\" to \"%s\" failed", tmp_file, filepath);
        MYMPD_LOG_ERRNO(errno);
        rm_file(tmp_file);
        return false;
    }
    return true;
}

bool write_data_to_file(sds filepath, const char *data, size_t data_len) {
    sds tmp_file = sdscatfmt(sdsempty(), "%S.XXXXXX", filepath);
    FILE *fp = open_tmp_file(tmp_file);
    if (fp == NULL) {
        FREE_SDS(tmp_file);
        return false;
    }
    size_t written = fwrite(data, 1, data_len, fp);
    bool write_rc = written == data_len ? true : false;
    bool rc = rename_tmp_file(fp, tmp_file, filepath, write_rc);
    FREE_SDS(tmp_file);
    return rc;
}
