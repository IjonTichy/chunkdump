#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <error.h>
#include <errno.h>
#include <libgen.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/stat.h>
#include <linux/limits.h>

#include <sys/types.h>
#include <dirent.h>

#include "crc.h"

#define PNG_HEADER "\x89PNG\r\n\x1A\n"
#define PNG_HEADERSIZE  8

#ifdef _WIN32
#define DIRSEP "\\"
#else
#define DIRSEP "/"
#endif

static char const* progname;

inline int max(int a, int b) { return a > b ? a : b; }

int is_big_endian(void)
{
    union 
    {
        uint32_t i;
        char c[4];
    } endian = {0x01FFFFFF};

    return endian.c[0] == 1;
}

void flip_int(uint32_t* x)
{
    *x = (*x << 24) + ((*x & 0x0000FF00) << 8) + ((*x & 0x00FF0000) >> 8) + (*x >> 24);
}

int guarantee_empty(char* dirpath)
{
    DIR* dir = opendir(dirpath);
    char newfile[PATH_MAX];
    struct dirent* entry;
    struct stat filestats;

    if (dir == NULL)
    {
        switch (errno)
        {
          default:
            fprintf(stderr, "%s: error: when opening \"%s\": %s", progname, dirpath, strerror(errno));
            return 1;
            break;

          case ENOENT:
            mkdir(dirpath, 0700);
            return;  // since obviously it's empty
        }
    }

    while ((entry = readdir(dir)) != NULL)
    {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) { continue; }

        memset(newfile, 0, sizeof(newfile));
        strcpy(newfile, dirpath);
        strcat(newfile, DIRSEP);
        strcat(newfile, entry->d_name);

        stat(newfile, &filestats);

        if (filestats.st_mode & S_IFDIR)
        {
            guarantee_empty(newfile);
        }
        remove(newfile);
    }
}

char* dumpChunk(char* outdir, int index, char* name, char* data, uint32_t dsize)
{
    char* outname = malloc(PATH_MAX);
    char indexstr[16];
    
    sprintf(indexstr, "%04d-", index);
    sprintf(outname, "%s%s%s%s.dat", outdir, DIRSEP, indexstr, name);

    FILE* outfd = fopen(outname, "wb");

    fwrite(data, 1, dsize, outfd);
    fclose(outfd);

    return outname;
}

int dumpPNGchunks(char* pngname)
{
    FILE* png = fopen(pngname, "rb");
    FILE* chunkOut = NULL;
    char* basepngname;
    char* pngname_noext;
    char outdir[PATH_MAX];
    char* dumpname;
    int index = 0;

    char topHeaderBuffer[PNG_HEADERSIZE];
    uint32_t chunkSize;
    uint32_t origCRC, checkCRC;
    char sizeBuffer[4];
    char headerBuffer[5];
    char crcBuffer[4];
    char* dataBuffer;

    if (png == NULL)
    {
        error(0, errno, "error: \"%s\"", pngname);
        return 1;
    }

    fread(topHeaderBuffer, 1, PNG_HEADERSIZE, png);

    if (ferror(png))
    {
        fprintf(stderr, "%s: error: \"%s\": %s\n", progname, pngname, strerror(ferror(png)));
        return 1;
    }

    if (memcmp(topHeaderBuffer, PNG_HEADER, PNG_HEADERSIZE))
    {
        fprintf(stderr, "%s: error: \"%s\" not a valid PNG file\n", progname, pngname);
        return 1;
    }

    basepngname = malloc(PATH_MAX);
    pngname_noext = malloc(255);

    strcpy(basepngname, basename(pngname)); // this is so strtok doesn't segfault
    strcpy(pngname_noext, strtok(basepngname, "."));  // this is so we have a unique copy

    free(basepngname);

    getcwd(outdir, PATH_MAX);
    strcat(outdir, DIRSEP);
    strcat(outdir, pngname_noext);

    if (guarantee_empty(outdir))
    {
        return 1;
    }

    while (1)
    {
        memset(sizeBuffer,   0, sizeof(sizeBuffer));
        memset(headerBuffer, 0, sizeof(headerBuffer));
        memset(crcBuffer,    0, sizeof(crcBuffer));

        if (fread(sizeBuffer, 1, 4, png) != 4) { return; }

        fread(headerBuffer, 1, 4, png);

        memcpy(&chunkSize, sizeBuffer, sizeof(uint32_t));

        if (!is_big_endian()) { flip_int(&chunkSize); }

        dataBuffer = malloc(chunkSize);

        fread(dataBuffer, 1, chunkSize, png);
        fread(crcBuffer, 1, 4, png);

        memcpy(&origCRC, crcBuffer, sizeof(uint32_t));
        if (!is_big_endian()) { flip_int(&origCRC); }

        checkCRC = update_crc(0xFFFFFFFFL, headerBuffer, 4);
        checkCRC = update_crc(checkCRC, dataBuffer, chunkSize) ^ 0xFFFFFFFFL;

        if (checkCRC != origCRC)
        {
            fprintf(stderr, "%s: warning: %s chunk in \"%s\" failed CRC", progname, headerBuffer, pngname);
        }

        dumpname = dumpChunk(outdir, index, headerBuffer, dataBuffer, chunkSize);

        printf("(%s).%s -> %s\n", pngname, headerBuffer, dumpname);
        free(dumpname);

        free(dataBuffer);
        index++;
    }

    free(basepngname);
    free(pngname_noext);
}

int main(int argc, char** argv)
{
    int i;
    int ret = 0;
    progname = basename(argv[0]);

    if (argc == 1)
    {
        fprintf(stderr, "%s: error: no filenames provided\n", progname);
        return 1;
    }

    for (i = 1; i < argc; i++)
    {
        ret += dumpPNGchunks(argv[i]);
    }
    
    return ret;
}
