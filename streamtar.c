#define _FILE_OFFSET_BITS 64

#include <errno.h> /* errno */
#include <fcntl.h> /* open */
#include <stdio.h> /* printf */
#include <stdlib.h> /* exit */
#include <string.h> /* memset */
#include <sys/file.h> /* flock */
#include <time.h> /* time */
#include <unistd.h> /* lseek */

#include <libtar.h>

#ifndef DEBUG_WRITES
	#define debug_writes 0
#else
	#define debug_writes 1
#endif

off_t _err(off_t expected, char const * name, off_t result)
{
    if (expected ? expected != result : result < 0) {
        perror(name);
        exit(errno);
    }
    return result;
}
#define err(func, ...) _err(0, #func, func(__VA_ARGS__))
#define errneq(expected, func, ...) _err(expected, #func, func(__VA_ARGS__))

void append(TAR *tar, off_t header_offset, time_t mtime, char * buffer, size_t length)
{
    th_set_mtime(tar, mtime); // last modification time
    size_t offset = length % T_BLOCKSIZE;
    th_set_size(tar, th_get_size(tar) + length); // size
    int_to_oct(th_crc_calc(tar), tar->th_buf.chksum, 8); // chksum

    if (offset != 0) {
        size_t padding = T_BLOCKSIZE - offset;
        memset(buffer + length, 0, padding);
        length += padding;
    }

    int fd = tar_fd(tar);

    err(flock, fd, LOCK_SH);

    /* first seek to header and write header, so seeking past the end on next open will make a valid file with hole and timestamp of missing data */
    if (debug_writes) {
        char dbgnam[256];
        sprintf(dbgnam, "%lld-debug_writes-header-%lld.bin", (long long)time(0), (long long)header_offset);
        int dbgfd = err(open, dbgnam, O_WRONLY | O_CREAT, 0600);
        errneq(T_BLOCKSIZE, write, dbgfd, &tar->th_buf, T_BLOCKSIZE);
        close(dbgfd);
    }
    errneq(header_offset, lseek, fd, header_offset, SEEK_SET);
    errneq(T_BLOCKSIZE, write, fd, &tar->th_buf, T_BLOCKSIZE);

    /* seek back to end and write next data */
    if (debug_writes) {
        char dbgnam[256];
        sprintf(dbgnam, "%lld-debug_writes-data-end.bin", (long long)time(0));
        int dbgfd = err(open, dbgnam, O_WRONLY | O_CREAT, 0600);
        errneq(length, write, dbgfd, buffer, length);
        close(dbgfd);
    }
    if ( /* if all zeros, write sparsely */
        length >= sizeof(long) &&
        0 == *(long*)buffer &&
        !memcmp(buffer, buffer + sizeof(long), length - sizeof(long))
    ) {
        off_t sparsesize = err(lseek, fd, length, SEEK_END);
        err(ftruncate, fd, sparsesize);
    } else {
        err(lseek, fd, 0, SEEK_END);
        errneq(length, write, fd, buffer, length);
    }

    err(flock, fd, LOCK_UN);
}

int main(int argc, char * const * argv)
{
    if (argc != 3) {
        printf(
            "\n"
            "Usage: streamtar <tarball>.tar <streamfilename>\n"
            "\n"
            "streamtar appends live data from stdin to a tar file.\n"
            "data is preserved in the event of a crash.\n"
            "\n"
        );
        return -1;
    }

    char *outer_fn = argv[1];
    char *inner_fn = argv[2];

    TAR * tar;
    int fd = err(open, outer_fn, O_RDWR | O_CREAT, 0600);
    err(tar_fdopen, &tar, fd, outer_fn, NULL, 0, 0600, TAR_VERBOSE | TAR_NOOVERWRITE);
    off_t hpos = 0;

    /* read existing records to seek to correct end for short writes */
    while (err(th_read, tar) == 0) {
        if (tar->th_buf.name[0] == 0) {
            // EOF indicator
            break;
        }
        off_t size = th_get_size(tar);
        off_t extra = size % T_BLOCKSIZE;
        off_t total = extra > 0 ? size - extra + T_BLOCKSIZE : size;
        hpos = err(lseek, fd, total, SEEK_CUR);
    }

    /* write new record */

    memset(&tar->th_buf, 0, sizeof(struct tar_header));
    th_set_type(tar, S_IFREG);
    th_set_path(tar, inner_fn);
    th_set_user(tar, getuid());
    th_set_group(tar, getgid());
    th_set_mode(tar, S_IFREG | 0444);
    th_finish(tar); // magic, chksum

    th_set_mtime(tar, time(0)); // last modification time
    th_set_size(tar, 0); // size
    int_to_oct(th_crc_calc(tar), tar->th_buf.chksum, 8); // chksum

    ssize_t just_read = 0, pending = 0, offset = 0;
    off_t remaining = 077777777777; // libtar doesn't have size extensions, so this is max filesize
    time_t mtime = th_get_mtime(tar);
    char buffer[T_BLOCKSIZE * 1024];
    do {
        mtime = time(0);
        pending = just_read + offset;
        offset = pending % T_BLOCKSIZE;
        size_t size = pending - offset;
        append(tar, hpos, mtime, buffer, size);
        remaining -= size;
        memcpy(buffer, buffer + size, offset);
    } while ((just_read = read(0,
                               buffer + offset,
                               (remaining < sizeof(buffer) ? remaining : sizeof(buffer)) - offset
                              )) > 0);

    /* write final unwritten chunk buffer */
    /* this isn't written in _err() because the only midway terminations relate to writing. */
    append(tar, hpos, mtime, buffer, offset);
    remaining -= offset;

    if (errno) {
        perror("read");
    }

    th_print_long_ls(tar);

    if (remaining == 0) {
	printf("Reached max libtar filesize.  Start new file.\n");
    }

    return errno;
}
