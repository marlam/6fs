/*
 * Copyright (C) 2023
 * Martin Lambers <marlam@marlam.de>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <cstddef>
#include <climits>
#include <cstring>
#include <cstdlib>
#include <cassert>

#define FUSE_USE_VERSION FUSE_MAKE_VERSION(3, 14)
#include <fuse.h>
#include <fuse_opt.h>

#include <sodium.h>

#include "index.hpp"
#include "logger.hpp"
#include "sixfs.hpp"
#include "dump.hpp"


/* Helper functions to convert between SixFS and system types */

mode_t toSysMode(uint16_t typeAndMode)
{
    mode_t m = 0;
    if ((typeAndMode & TypeMask) == TypeSOCK)
        m = S_IFSOCK;
    else if ((typeAndMode & TypeMask) == TypeLNK)
        m = S_IFLNK;
    else if ((typeAndMode & TypeMask) == TypeREG)
        m = S_IFREG;
    else if ((typeAndMode & TypeMask) == TypeBLK)
        m = S_IFBLK;
    else if ((typeAndMode & TypeMask) == TypeDIR)
        m = S_IFDIR;
    else if ((typeAndMode & TypeMask) == TypeCHR)
        m = S_IFCHR;
    else if ((typeAndMode & TypeMask) == TypeFIFO)
        m = S_IFIFO;
    if (typeAndMode & ModeSUID)
        m |= S_ISUID;
    if (typeAndMode & ModeSGID)
        m |= S_ISGID;
    if (typeAndMode & ModeSVTX)
        m |= S_ISVTX;
    if (typeAndMode & ModeRUSR)
        m |= S_IRUSR;
    if (typeAndMode & ModeWUSR)
        m |= S_IWUSR;
    if (typeAndMode & ModeXUSR)
        m |= S_IXUSR;
    if (typeAndMode & ModeRGRP)
        m |= S_IRGRP;
    if (typeAndMode & ModeWGRP)
        m |= S_IWGRP;
    if (typeAndMode & ModeXGRP)
        m |= S_IXGRP;
    if (typeAndMode & ModeROTH)
        m |= S_IROTH;
    if (typeAndMode & ModeWOTH)
        m |= S_IWOTH;
    if (typeAndMode & ModeXOTH)
        m |= S_IXOTH;
    return m;
}

uint16_t toTypeAndMode(mode_t mode)
{
    uint16_t m = 0;
    if (S_ISSOCK(mode))
        m = TypeSOCK;
    else if (S_ISLNK(mode))
        m = TypeLNK;
    else if (S_ISREG(mode))
        m = TypeREG;
    else if (S_ISBLK(mode))
        m = TypeBLK;
    else if (S_ISDIR(mode))
        m = TypeDIR;
    else if (S_ISCHR(mode))
        m = TypeCHR;
    else if (S_ISFIFO(mode))
        m = TypeFIFO;
    if (mode & S_ISUID)
        m |= ModeSUID;
    if (mode & S_ISGID)
        m |= ModeSGID;
    if (mode & S_ISVTX)
        m |= ModeSVTX;
    if (mode & S_IRUSR)
        m |= ModeRUSR;
    if (mode & S_IWUSR)
        m |= ModeWUSR;
    if (mode & S_IXUSR)
        m |= ModeXUSR;
    if (mode & S_IRGRP)
        m |= ModeRGRP;
    if (mode & S_IWGRP)
        m |= ModeWGRP;
    if (mode & S_IXGRP)
        m |= ModeXGRP;
    if (mode & S_IROTH)
        m |= ModeROTH;
    if (mode & S_IWOTH)
        m |= ModeWOTH;
    if (mode & S_IXOTH)
        m |= ModeXOTH;
    return m;
}

Time toTime(const struct timespec& tv)
{
    Time t;
    t.seconds = tv.tv_sec;
    t.nanoseconds = tv.tv_nsec;
    return t;
}

void inodeToStat(uint64_t inodeIndex, const Inode& inode, struct stat* stbuf)
{
    stbuf->st_ino = inodeIndex;
    stbuf->st_mode = toSysMode(inode.typeAndMode);
    stbuf->st_nlink = inode.nlink;
    stbuf->st_uid = inode.uid;
    stbuf->st_gid = inode.gid;
    stbuf->st_rdev = inode.rdev;
    stbuf->st_size = inode.size;
    stbuf->st_blocks = inode.size / 512;
    stbuf->st_atim.tv_sec = inode.atime.seconds;
    stbuf->st_atim.tv_nsec = inode.atime.nanoseconds;
    stbuf->st_mtim.tv_sec = inode.mtime.seconds;
    stbuf->st_mtim.tv_nsec = inode.mtime.nanoseconds;
    stbuf->st_ctim.tv_sec = inode.ctime.seconds;
    stbuf->st_ctim.tv_nsec = inode.ctime.nanoseconds;
}


/* FUSE Operations */

static void* sixfs_init(struct fuse_conn_info* conn, struct fuse_config* cfg)
{
    SixFS* sixfs = static_cast<SixFS*>(fuse_get_context()->private_data);

    (void)conn;
    cfg->direct_io = 1;         // enabled to get sane read() and write() behaviour; I don't understand how the
                                // alternative should be implemented
    cfg->use_ino = 1;
    cfg->kernel_cache = (sixfs->isRemote() ? 0 : 1);
    cfg->nullpath_ok = 1;

    return fuse_get_context()->private_data;
}

static void sixfs_destroy(void* private_data)
{
    SixFS* sixfs = static_cast<SixFS*>(private_data);
    sixfs->unmount(); // we cannot handle errors returned by unmount() here
}

static int sixfs_getattr(const char* path, struct stat* stbuf, struct fuse_file_info* fi)
{
    logger.log(Logger::Debug, "sixfs_getattr(\"%s\")", path);
    SixFS* sixfs = static_cast<SixFS*>(fuse_get_context()->private_data);
    Handle* handle = (fi ? reinterpret_cast<Handle*>(fi->fh) : nullptr);

    uint64_t inodeIndex = InvalidIndex;
    Inode inode;
    int r = sixfs->getAttr(handle, path, &inodeIndex, &inode);
    if (r < 0)
        return r;

    inodeToStat(inodeIndex, inode, stbuf);
    return 0;
}

static int sixfs_opendir(const char* path, struct fuse_file_info* fi)
{
    logger.log(Logger::Debug, "sixfs_opendir(\"%s\")", path);
    SixFS* sixfs = static_cast<SixFS*>(fuse_get_context()->private_data);

    Handle* handle;
    int r = sixfs->openDir(path, &handle);
    if (r < 0)
        return r;

    fi->fh = reinterpret_cast<uint64_t>(handle);
    return 0;
}

static int sixfs_readdir(const char* path,
        void* buf,
        fuse_fill_dir_t filler,
        off_t offset,
        struct fuse_file_info* fi,
        enum fuse_readdir_flags flags)
{
    logger.log(Logger::Debug, "sixfs_readdir(\"%s\") offset=%lu", path, uint64_t(offset));
    SixFS* sixfs = static_cast<SixFS*>(fuse_get_context()->private_data);
    Handle* handle = reinterpret_cast<Handle*>(fi->fh);

    if (flags & FUSE_READDIR_PLUS) {
        for (uint64_t o = offset; ; o++) {
            const char* name;
            struct stat stbuf;
            if (o == 0) {
                name = ".";
                inodeToStat(handle->inodeIndex(), handle->inode(), &stbuf);
            } else if (o == 1) {
                name = "..";
                // this entry does not get a statbuf pointer (see below); that
                // seems to work anyway
            } else {
                Dirent dirent;
                Inode inode;
                int r = sixfs->readDirentPlus(handle, o - 2, &dirent, &inode);
                if (r == -EINVAL)
                    break;
                else if (r < 0)
                    return r;
                name = dirent.name;
                inodeToStat(dirent.inodeIndex, inode, &stbuf);
            }
            if (filler(buf, name, o == 1 ? nullptr : &stbuf, o + 1, static_cast<fuse_fill_dir_flags>(FUSE_FILL_DIR_PLUS)) == 1) {
                break;
            }
        }
    } else {
        for (uint64_t o = offset; ; o++) {
            const char* name;
            if (o == 0) {
                name = ".";
            } else if (o == 1) {
                name = "..";
            } else {
                Dirent dirent;
                int r = sixfs->readDirent(handle, o - 2, &dirent);
                if (r == -EINVAL)
                    break;
                else if (r < 0)
                    return r;
                name = dirent.name;
            }
            if (filler(buf, name, nullptr, o + 1, static_cast<fuse_fill_dir_flags>(0)) == 1) {
                break;
            }
        }
    }

    return 0;
}

static int sixfs_releasedir(const char* path, struct fuse_file_info* fi)
{
    logger.log(Logger::Debug, "sixfs_releasedir(\"%s\")", path);
    SixFS* sixfs = static_cast<SixFS*>(fuse_get_context()->private_data);
    Handle* handle = reinterpret_cast<Handle*>(fi->fh);
    return sixfs->closeDir(handle);
}

static int sixfs_mkdir(const char* path, mode_t mode)
{
    logger.log(Logger::Debug, "sixfs_mkdir(\"%s\")", path);
    SixFS* sixfs = static_cast<SixFS*>(fuse_get_context()->private_data);
    return sixfs->mkdir(path, toTypeAndMode(mode));
}

static int sixfs_rmdir(const char* path)
{
    logger.log(Logger::Debug, "sixfs_rmdir(\"%s\")", path);
    SixFS* sixfs = static_cast<SixFS*>(fuse_get_context()->private_data);
    return sixfs->rmdir(path);
}

static int sixfs_mknod(const char* path, mode_t mode, dev_t rdev)
{
    logger.log(Logger::Debug, "sixfs_mknod(\"%s\")", path);
    SixFS* sixfs = static_cast<SixFS*>(fuse_get_context()->private_data);
    return sixfs->mknod(path, toTypeAndMode(mode), rdev);
}

static int sixfs_unlink(const char* path)
{
    logger.log(Logger::Debug, "sixfs_unlink(\"%s\")", path);
    SixFS* sixfs = static_cast<SixFS*>(fuse_get_context()->private_data);
    return sixfs->unlink(path);
}

static int sixfs_symlink(const char* target, const char* linkpath)
{
    logger.log(Logger::Debug, "sixfs_symlink(\"%s\", \"%s\")", target, linkpath);
    SixFS* sixfs = static_cast<SixFS*>(fuse_get_context()->private_data);
    return sixfs->symlink(target, linkpath);
}

static int sixfs_readlink(const char* path, char* buf, size_t bufsize)
{
    logger.log(Logger::Debug, "sixfs_readlink(\"%s\")", path);
    SixFS* sixfs = static_cast<SixFS*>(fuse_get_context()->private_data);
    return sixfs->readlink(path, buf, bufsize);
}

static int sixfs_link(const char* oldpath, const char* newpath)
{
    logger.log(Logger::Debug, "sixfs_link(\"%s\", \"%s\")", oldpath, newpath);
    SixFS* sixfs = static_cast<SixFS*>(fuse_get_context()->private_data);
    return sixfs->link(oldpath, newpath);
}

static int sixfs_rename(const char* oldpath, const char* newpath, unsigned int flags)
{
    logger.log(Logger::Debug, "sixfs_rename(\"%s\", \"%s\")", oldpath, newpath);
    SixFS* sixfs = static_cast<SixFS*>(fuse_get_context()->private_data);
    return sixfs->rename(oldpath, newpath,
              (flags & RENAME_EXCHANGE)  ? SixFS::RenameExchange
            : (flags & RENAME_NOREPLACE) ? SixFS::RenameNoreplace
            : SixFS::RenameNormal);
}

static int sixfs_chmod(const char* path, mode_t mode, struct fuse_file_info* fi)
{
    logger.log(Logger::Debug, "sixfs_chmod(\"%s\")", path);
    SixFS* sixfs = static_cast<SixFS*>(fuse_get_context()->private_data);
    Handle* handle = (fi ? reinterpret_cast<Handle*>(fi->fh) : nullptr);
    return sixfs->chmod(handle, path, toTypeAndMode(mode));
}

static int sixfs_chown(const char* path, uid_t uid, gid_t gid, struct fuse_file_info* fi)
{
    logger.log(Logger::Debug, "sixfs_chown(\"%s\")", path);
    SixFS* sixfs = static_cast<SixFS*>(fuse_get_context()->private_data);
    Handle* handle = (fi ? reinterpret_cast<Handle*>(fi->fh) : nullptr);
    return sixfs->chown(handle, path, uid, gid);
}

static int sixfs_utimens(const char* path, const struct timespec tv[2], struct fuse_file_info* fi)
{
    logger.log(Logger::Debug, "sixfs_utimens(\"%s\")", path);
    SixFS* sixfs = static_cast<SixFS*>(fuse_get_context()->private_data);
    Handle* handle = (fi ? reinterpret_cast<Handle*>(fi->fh) : nullptr);
    Time atime, mtime, ctime;
    bool updateAtime = true;
    bool updateMtime = true;
    ctime = Time::now();
    if (tv[0].tv_nsec == UTIME_NOW)
        atime = ctime;
    else if (tv[0].tv_nsec == UTIME_OMIT)
        updateAtime = false;
    else
        atime = toTime(tv[0]);
    if (tv[1].tv_nsec == UTIME_NOW)
        mtime = ctime;
    else if (tv[1].tv_nsec == UTIME_OMIT)
        updateMtime = false;
    else
        mtime = toTime(tv[1]);
    return sixfs->utimens(handle, path, updateAtime, atime, updateMtime, mtime, true, ctime);
}

static int sixfs_truncate(const char* path, off_t length, struct fuse_file_info* fi)
{
    logger.log(Logger::Debug, "sixfs_truncate(\"%s\", %ld)", path, length);
    SixFS* sixfs = static_cast<SixFS*>(fuse_get_context()->private_data);
    Handle* handle = (fi ? reinterpret_cast<Handle*>(fi->fh) : nullptr);
    return sixfs->truncate(handle, path, length);
}

static int sixfs_open(const char* path, struct fuse_file_info* fi)
{
    logger.log(Logger::Debug, "sixfs_open(\"%s\")", path);
    SixFS* sixfs = static_cast<SixFS*>(fuse_get_context()->private_data);

    Handle* handle;
    int r = sixfs->open(path, !((fi->flags & O_RDWR) || (fi->flags & O_WRONLY)),
            (fi->flags & O_TRUNC), (fi->flags & O_APPEND), &handle);
    if (r < 0)
        return r;

    fi->fh = reinterpret_cast<uint64_t>(handle);
    return 0;
}

static int sixfs_read(const char* path, char* buf, size_t count, off_t offset, struct fuse_file_info* fi)
{
    logger.log(Logger::Debug, "sixfs_read(\"%s\", offset=%ld, count=%zu)", path, offset, count);
    SixFS* sixfs = static_cast<SixFS*>(fuse_get_context()->private_data);
    Handle* handle = reinterpret_cast<Handle*>(fi->fh);
    return sixfs->read(handle, offset, reinterpret_cast<unsigned char*>(buf), count);
}

static int sixfs_write(const char* path, const char* buf, size_t count, off_t offset, struct fuse_file_info* fi)
{
    logger.log(Logger::Debug, "sixfs_write(\"%s\", offset=%ld, count=%zu)", path, offset, count);
    SixFS* sixfs = static_cast<SixFS*>(fuse_get_context()->private_data);
    Handle* handle = reinterpret_cast<Handle*>(fi->fh);
    return sixfs->write(handle, offset, reinterpret_cast<const unsigned char*>(buf), count);
}

static int sixfs_release(const char* path, struct fuse_file_info* fi)
{
    logger.log(Logger::Debug, "sixfs_release(\"%s\")", path);
    SixFS* sixfs = static_cast<SixFS*>(fuse_get_context()->private_data);
    return sixfs->close(reinterpret_cast<Handle*>(fi->fh));
}

static int sixfs_statfs(const char* /* ignored */, struct statvfs* statfs)
{
    logger.log(Logger::Debug, "sixfs_statfs()");
    SixFS* sixfs = static_cast<SixFS*>(fuse_get_context()->private_data);

    size_t blockSize;
    size_t maxNameLen;
    uint64_t maxBlockCount;
    uint64_t freeBlockCount;
    uint64_t maxInodeCount;
    uint64_t freeInodeCount;
    int r = sixfs->statfs(&blockSize, &maxNameLen, &maxBlockCount, &freeBlockCount, &maxInodeCount, &freeInodeCount);
    if (r < 0)
        return r;

    statfs->f_bsize = blockSize;
    statfs->f_frsize = blockSize;
    statfs->f_blocks = maxBlockCount;
    statfs->f_bfree = freeBlockCount;
    statfs->f_bavail = freeBlockCount;
    statfs->f_files = maxInodeCount;
    statfs->f_ffree = freeInodeCount;
    statfs->f_namemax = maxNameLen;
    return 0;
}

static void sixfsPrintHelp(const char *progname)
{
    printf("usage: %s [options] <mountpoint>\n\n", progname);
    printf("File-system specific options:\n"
            "    --dir=<dir>            the directory containing the 6fs files to mount\n"
            "    --max-size=<size>      max size in bytes; suffixes K, M, G, T are supported\n"
            "    --key=<keyfile>        activate encryption and read key from keyfile\n"
            "    --log=<logfile>        log messages to logfile or to syslog (default) if file name is empty\n"
            "    --log-level=<level>    set minimum level for log messages (debug, info, warning, error)\n"
            "    --punch-holes=0|1      punch holes for unused blocks into the block data file to save disk space\n"
            "  Only for debugging:\n"
            "    --dump-inode=<i>       dump inode\n"
            "    --dump-tree=<i>        dump slot tree of inode\n"
            "    --dump-dirent=<i>      dump directory entry\n"
            "    --dump-slot-block=<i>  dump block, interpreted as slot indirection block\n"
            "    --dump-data-block=<i>  dump block, interpreted as data block\n");
}

static int getUint64(const char* s, uint64_t* val, const char** ep)
{
    errno = 0;
    char* endptr;
    int r = 0;
    unsigned long long tmp = ::strtoull(s, &endptr, 10);
    if (errno != 0) {
        r = -errno;
    } else if (endptr == s) {
        r = -EINVAL;
    }
    if (r == 0) {
        *val = tmp;
        if (ep)
            *ep = endptr;
    }
    return r;
}

static int getMaxSize(const char* s, uint64_t* val)
{
    const char* endptr;
    int r = getUint64(s, val, &endptr);
    if (r != 0)
        return r;
    unsigned long long suffix;
    bool valid = true;
    if (*endptr == '\0')
        suffix = 1ULL;
    else if (strcmp(endptr, "K") == 0)
        suffix = 1024ULL;
    else if (strcmp(endptr, "M") == 0)
        suffix = 1024ULL * 1024ULL;
    else if (strcmp(endptr, "G") == 0)
        suffix = 1024ULL * 1024ULL * 1024ULL;
    else if (strcmp(endptr, "T") == 0)
        suffix = 1024ULL * 1024ULL * 1024ULL * 1024ULL;
    else
        valid = false;
    if (std::numeric_limits<unsigned long long>::max() / suffix < *val)
        valid = false;
    if (!valid) {
        return -EINVAL;
    } else {
        *val *= suffix;
        return 0;
    }
}

typedef struct
{
    int showHelp;
    const char* dirName;
    const char* maxSize;
    const char* keyName;
    const char* logName;
    const char* logLevel;
    const char* punchHoles;
    const char* dumpInode;
    const char* dumpTree;
    const char* dumpDirent;
    const char* dumpSBlock;
    const char* dumpDBlock;
} SixfsOptionsStruct;

int main(int argc, char *argv[])
{
    /* Handle command line */

    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    SixfsOptionsStruct sixfsOptionsStruct = {
        .showHelp = 0,
        .dirName = nullptr,
        .maxSize = nullptr,
        .keyName = nullptr,
        .logName = nullptr,
        .logLevel = nullptr,
        .punchHoles = nullptr,
        .dumpInode = nullptr,
        .dumpTree = nullptr,
        .dumpDirent = nullptr,
        .dumpSBlock = nullptr,
        .dumpDBlock = nullptr
    };
    struct fuse_opt sixfsOptions[] = {
        { "--dir=%s",             offsetof(SixfsOptionsStruct, dirName),    1 },
        { "--max-size=%s",        offsetof(SixfsOptionsStruct, maxSize),    1 },
        { "--key=%s",             offsetof(SixfsOptionsStruct, keyName),    1 },
        { "--log=%s",             offsetof(SixfsOptionsStruct, logName),    1 },
        { "--log-level=%s",       offsetof(SixfsOptionsStruct, logLevel),   1 },
        { "--punch-holes=%s",     offsetof(SixfsOptionsStruct, punchHoles), 1 },
        { "--dump-inode=%s",      offsetof(SixfsOptionsStruct, dumpInode),  1 },
        { "--dump-tree=%s",       offsetof(SixfsOptionsStruct, dumpTree),   1 },
        { "--dump-dirent=%s",     offsetof(SixfsOptionsStruct, dumpDirent), 1 },
        { "--dump-slot-block=%s", offsetof(SixfsOptionsStruct, dumpSBlock), 1 },
        { "--dump-data-block=%s", offsetof(SixfsOptionsStruct, dumpDBlock), 1 },
        { "--help",               offsetof(SixfsOptionsStruct, showHelp),   1 },
        { "-h",                   offsetof(SixfsOptionsStruct, showHelp),   1 },
        FUSE_OPT_END
    };
    // parse command line
    if (fuse_opt_parse(&args, &sixfsOptionsStruct, sixfsOptions, nullptr) < 0) {
        return 1;
    }
    if (sixfsOptionsStruct.showHelp) {
        sixfsPrintHelp(argv[0]);
        fuse_opt_add_arg(&args, "--help");
        args.argv[0][0] = '\0';
    }
    // set required permissions options
    fuse_opt_add_arg(&args, "-o");
    fuse_opt_add_arg(&args, "default_permissions,kernel_cache");
    // interpret options
    uint64_t maxSize = 0;
    if (sixfsOptionsStruct.maxSize) {
        if (getMaxSize(sixfsOptionsStruct.maxSize, &maxSize) != 0) {
            fprintf(stderr, "Invalid max size\n");
            return 1;
        }
    }
    Logger::Level logLevel = Logger::Warning;
    if (sixfsOptionsStruct.logLevel) {
        if (strcmp(sixfsOptionsStruct.logLevel, "debug") == 0)
            logLevel = Logger::Debug;
        else if (strcmp(sixfsOptionsStruct.logLevel, "info") == 0)
            logLevel = Logger::Info;
        else if (strcmp(sixfsOptionsStruct.logLevel, "warning") == 0)
            logLevel = Logger::Warning;
        else if (strcmp(sixfsOptionsStruct.logLevel, "error") == 0)
            logLevel = Logger::Error;
        else {
            fprintf(stderr, "Invalid log level %s (valid options: debug, info, warning, error)\n",
                    sixfsOptionsStruct.logLevel);
            return 1;
        }
    }
    logger.setArgv0(argv[0]);
    logger.setLevel(logLevel);
    if (sixfsOptionsStruct.logName) {
        logger.setOutput(sixfsOptionsStruct.logName);
    }
    std::vector<unsigned char> key;
    if (sixfsOptionsStruct.keyName) {
        FILE* f = fopen(sixfsOptionsStruct.keyName, "r");
        if (!f) {
            fprintf(stderr, "Cannot open key file %s: %s\n", sixfsOptionsStruct.keyName, strerror(errno));
            return 1;
        }
        key.resize(crypto_stream_salsa20_KEYBYTES);
        if (::fread(key.data(), crypto_stream_salsa20_KEYBYTES, 1, f) != 1) {
            if (::ferror(f)) {
                fprintf(stderr, "Cannot read key from file %s: %s\n", sixfsOptionsStruct.keyName, strerror(errno));
            } else {
                fprintf(stderr, "Cannot read key from file %s: not enough data\n", sixfsOptionsStruct.keyName);
            }
            fclose(f);
            return 1;
        }
        fclose(f);
    }
    bool punchHoles = false;
    if (sixfsOptionsStruct.punchHoles) {
        if (strcmp(sixfsOptionsStruct.punchHoles, "0") != 0 && strcmp(sixfsOptionsStruct.punchHoles, "1") != 0) {
            fprintf(stderr, "Invalid argument to option --punch-holes\n");
            return 1;
        }
        punchHoles = (strcmp(sixfsOptionsStruct.punchHoles, "1") == 0);
    }
    std::string dirName;
    if (sixfsOptionsStruct.dirName) {
        dirName = std::string(sixfsOptionsStruct.dirName);
    } else {
        fprintf(stderr, "Option --dir is missing\n");
        return 1;
    }

    /* Handle the debugging options */
    if (!sixfsOptionsStruct.showHelp
            && (sixfsOptionsStruct.dumpInode
                || sixfsOptionsStruct.dumpTree
                || sixfsOptionsStruct.dumpDirent
                || sixfsOptionsStruct.dumpSBlock
                || sixfsOptionsStruct.dumpDBlock)) {
        return dump(dirName, key,
                sixfsOptionsStruct.dumpInode,
                sixfsOptionsStruct.dumpTree,
                sixfsOptionsStruct.dumpDirent,
                sixfsOptionsStruct.dumpSBlock,
                sixfsOptionsStruct.dumpDBlock);
    }

    /* Initialize sixfs */

    if (!sixfsOptionsStruct.showHelp) {
        if (sodium_init() < 0) {
            fprintf(stderr, "Cannot initialize libsodium\n");
            return 1;
        }
    }
    SixFS sixfs(dirName, maxSize, key, punchHoles);
    if (!sixfsOptionsStruct.showHelp) {
        std::string errStr;
        int r = sixfs.mount(errStr);
        if (r < 0) {
            fprintf(stderr, "Cannot initialize 6fs: %s\n", errStr.c_str());
            return 1;
        }
    }

    /* Run main loop */

    const struct fuse_operations sixfsOperations = {
        .getattr         = sixfs_getattr,
        .readlink        = sixfs_readlink,
        .mknod           = sixfs_mknod,
        .mkdir           = sixfs_mkdir,
        .unlink          = sixfs_unlink,
        .rmdir           = sixfs_rmdir,
        .symlink         = sixfs_symlink,
        .rename          = sixfs_rename,
        .link            = sixfs_link,
        .chmod           = sixfs_chmod,
        .chown           = sixfs_chown,
        .truncate        = sixfs_truncate,
        .open            = sixfs_open,
        .read            = sixfs_read,
        .write           = sixfs_write,
        .statfs          = sixfs_statfs,
        .flush           = nullptr,              // not needed for us
        .release         = sixfs_release,
        .fsync           = nullptr,              // not needed for us
        .setxattr        = nullptr,              // xattr support not needed
        .getxattr        = nullptr,              // xattr support not needed
        .listxattr       = nullptr,              // xattr support not needed
        .removexattr     = nullptr,              // xattr support not needed
        .opendir         = sixfs_opendir,
        .readdir         = sixfs_readdir,
        .releasedir      = sixfs_releasedir,
        .fsyncdir        = nullptr,              // not needed for us
        .init            = sixfs_init,
        .destroy         = sixfs_destroy,
        .access          = nullptr,              // not needed because of default_permissions
        .create          = nullptr,              // not needed: fuse will use mknod+open
        .lock            = nullptr,              // not needed: kernel handles posix locks
        .utimens         = sixfs_utimens,
        .bmap            = nullptr,              // makes no sense for us
        .ioctl           = nullptr,              // makes no sense for us
        .poll            = nullptr,              // makes no sense for us
        .write_buf       = nullptr,              // apparently useful for avoiding data copies, but not needed
        .read_buf        = nullptr,              // apparently useful for avoiding data copies, but not needed
        .flock           = nullptr,              // not needed: kernel handles bsd locks
        .fallocate       = nullptr,              // TODO: support holes in files
        .copy_file_range = nullptr,              // TODO: optimized data copy
        .lseek           = nullptr               // TODO: support holes in files
    };
    int ret = fuse_main(args.argc, args.argv, &sixfsOperations, &sixfs);
    fuse_opt_free_args(&args);
    return ret;
}
