#pragma once
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <string.h>
#include "autosave100_data.h"

namespace CompleteGame {

static bool writeAutosaveFile(const char* path) {
    if(!path || !path[0]) return false;
    char dir[MAX_PATH];
    strncpy(dir, path, sizeof(dir));
    dir[sizeof(dir)-1] = '\0';
    char *slash = strrchr(dir, '\\');
    if (slash) {
        *slash = '\0';
        for (char *p = dir + 3; *p; p++) {
            if (*p == '\\') {
                *p = '\0';
                CreateDirectoryA(dir, NULL);
                *p = '\\';
            }
        }
        CreateDirectoryA(dir, NULL);
    }
    HANDLE h = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = WriteFile(h, AUTOSAVE100_BYTES, AUTOSAVE100_SIZE, &written, NULL);
    CloseHandle(h);
    return ok && written == AUTOSAVE100_SIZE;
}

static int writeAutosaveToDocFolder(const char* basePath) {
    if(!basePath || !basePath[0]) return 0;
    int count = 0;
    char search[MAX_PATH];
    snprintf(search, sizeof(search), "%s\\Ubisoft\\Driver San Francisco\\*", basePath);
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(search, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && strcmp(fd.cFileName, ".") != 0 && strcmp(fd.cFileName, "..") != 0) {
                char target[MAX_PATH];
                snprintf(target, sizeof(target), "%s\\Ubisoft\\Driver San Francisco\\%s\\savegames\\autosave.dat", basePath, fd.cFileName);
                if(writeAutosaveFile(target)) count++;
            }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }
    const char* subdirs[] = { "Player", "savegames", "SKIDROW", "reloaded", "default" };
    for (const char* sub : subdirs) {
        char target[MAX_PATH];
        if (strcmp(sub, "savegames") == 0) {
            snprintf(target, sizeof(target), "%s\\Ubisoft\\Driver San Francisco\\savegames\\autosave.dat", basePath);
        } else {
            snprintf(target, sizeof(target), "%s\\Ubisoft\\Driver San Francisco\\%s\\savegames\\autosave.dat", basePath, sub);
        }
        if (writeAutosaveFile(target)) count++;
    }
    return count;
}

static bool executeSavegameWrite() {
    int total = 0;
    char path[MAX_PATH] = {0};
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, path)) && path[0]) {
        total += writeAutosaveToDocFolder(path);
    }
    char pubPath[MAX_PATH] = {0};
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_COMMON_DOCUMENTS, NULL, SHGFP_TYPE_CURRENT, pubPath)) && pubPath[0]) {
        total += writeAutosaveToDocFolder(pubPath);
    }
    char envUser[MAX_PATH] = {0};
    if (GetEnvironmentVariableA("USERPROFILE", envUser, MAX_PATH) > 0 && envUser[0]) {
        char docs[MAX_PATH];
        snprintf(docs, sizeof(docs), "%s\\Documents", envUser);
        total += writeAutosaveToDocFolder(docs);
    }
    char envPub[MAX_PATH] = {0};
    if (GetEnvironmentVariableA("PUBLIC", envPub, MAX_PATH) > 0 && envPub[0]) {
        char docs[MAX_PATH];
        snprintf(docs, sizeof(docs), "%s\\Documents", envPub);
        total += writeAutosaveToDocFolder(docs);
    }
    return total > 0;
}

} // namespace CompleteGame
