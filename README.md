# About

6fs is a [FUSE](https://github.com/libfuse/libfuse) file system that stores all
data in six files on an existing file system. These six files grow and shrink
dynamically, and therefore always use only little more space than the data you
store in 6fs.

# Use Case

The main purpose of writing this was to learn more about how [FUSE](https://github.com/libfuse/libfuse)
works, and how file systems work in general. Since it is boring to write a program that
cannot actually do anything useful, the use case for 6fs is this:

6fs can store encrypted copies or backups of files on file systems that do not
provide the full set of Linux/UNIX file systems features, e.g. an external USB
device formatted with exfat, or a file server providing only SMB.

(However, in all honesty: performance is abysmal for anything but small or medium scale
use cases. That's to be expected with random-access read/write on top of six
files, combined with the very basic data structures used by this file system.)

# Features

- Stores all data in six dynamically growing and shrinking files
- Supports encryption, including metadata and directory structures
- Supports all standard UNIX file system features such as permissions, ownership,
  time stamps, device files, sockets, named pipes, symbolic links and so on
- Can also be used as a RAM disk, using six growing/shrinking arrays in memory

# Installation

6fs requires [libfuse](https://github.com/libfuse/libfuse) and
[libsodium](https://libsodium.org/).

It is based on autotools: use the typical `autoreconf -fi; ./configure; make; make
install` sequence to install it.

# Usage

Usage: `6fs [options] <mountpoint>`

Options:
- `--type=<mmap|file|mem>`: The storage type: mmap'ed file (the default), regular file,
  or memory.
- `--dir=<dir>`: The directory containing the six 6fs files (not required for type mem).
  The files will be automatically created if they do not exist yet.
- `--max-size=<size>`: Set a maximum size for the 6fs file system.
  Suffixes K, M, G, T are supported. Note that this limit is only approximate
  for performance reasons. Recommended for RAM disks.
- `--key=<keyfile>`: Activate encryption and read the 40-byte encryption key from the
  specified file. The key file should of course not be stored on the same medium as the
  6fs files.
- `--log=<logfile>`: Set a file to send log messages to. If the file name is
  empty, log messages are sent to syslog.
- `--log-level=<level>`: Set a minimum level for log messages (debug, info, warning, error).
  Default is warning.
- `--punch-holes=0|1`: Punch holes for unused blocks into the block data file to save disk space.
  Does not work on all file systems and costs performance. Disabled by default.

Example without encryption:
```
# Create mount point for 6fs
mkdir /mnt/6fs
# Mount 6fs, with its six data files residing in /mnt/usbdrive/6fs
6fs --dir=/mnt/usbdrive/6fs /mnt/6fs
# Use rsync to copy something into 6fs (just an example)
rsync -av /stuff/to/copy /mnt/6fs
# Unmount 6fs
fusermount -u /mnt/6fs
```

Example with encryption:
```
# Create random 40-byte key
dd if=/dev/urandom of=6fs-key bs=1 count=40
# Mount 6fs with encryption
6fs --dir=/mnt/usbdrive/6fs --key=6fs-key /mnt/6fs
```

# Internals

## Data Structure

The six files that 6fs is based on are the following:
- `inodemap.6fs`: bit map to manage inodes
- `inodedat.6fs`: inode data
- `direnmap.6fs`: bit map to manage directory entries
- `direndat.6fs`: directory entry data
- `blockmap.6fs`: bit map to manage data blocks
- `blockdat.6fs`: blocks of data, each containing 4096 bytes

The bit maps indicate with each bit (one or zero) whether the corresponding
inode / directory entry / data block is currently used (one) or free (zero).

The data files are simple one-dimensional arrays of inode, directory entry, or
block data.

Unused bits or array entries at the end of each file are removed so that the
files do not occupy more space than necessary. Moreover, with `--punch-holes=1`
unused data block entries are deallocated from the underlying file system if
that file system supports the `fallocate` flag `FALLOC_FL_PUNCH_HOLE`, so that
even unused data blocks in the middle of the array do not occupy disk space.

Each inode stores an index to the data associated with that inode (directory
entries or data blocks). That index uses multilevel indirection to
support up to 68853957121 entries, resulting in a maximum file size of 256.5
TiB.

## Encryption

Encryption is applied to the three array files, but not to the bit map files.
Each array entry is encrypted using the libsodium
[secretbox-easy](https://doc.libsodium.org/secret-key_cryptography/secretbox)
interface, and the nonce and authentication tag is stored together with the
encrypted data.

An attacker that can read the 6fs files can see how many inodes, directory
entries and data blocks your file system uses, but cannot see directory
hierarchies, file names and other metadata, or file contents.  An attacker that
additionally has write access to the 6fs files can of course corrupt or delete
them, but cannot modify encrypted data without being detected, and cannot
manipulate the data in a way that will reveal the key.
