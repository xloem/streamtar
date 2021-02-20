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
    errneq(header_offset, lseek, fd, header_offset, SEEK_SET);
    errneq(T_BLOCKSIZE, write, fd, &tar->th_buf, T_BLOCKSIZE);
    /* seek back to end and write next data */
    err(lseek, fd, 0, SEEK_END);
    errneq(length, write, fd, buffer, length);

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
        size_t size = th_get_size(tar);
        size_t extra = size % T_BLOCKSIZE;
        size_t total = extra > 0 ? size - extra + T_BLOCKSIZE : size;
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

    ssize_t total = 0, offset = 0;
    time_t mtime = th_get_mtime(tar);
    char buffer[T_BLOCKSIZE * 1024];
    do {
        mtime = time(0);
        total += offset;
        offset = total % T_BLOCKSIZE;
        size_t size = total - offset;
        append(tar, hpos, mtime, buffer, size);
        memcpy(buffer, buffer + size, offset);
    } while ((total = read(0, buffer + offset, sizeof(buffer) - offset)) > 0);

    /* write final chunk buffer */
    /* this isn't written in _err() because the only midway terminations relate to writing. */
    append(tar, hpos, mtime, buffer, offset);

    if (errno) {
        perror("read");
    }

    th_print_long_ls(tar);

    return errno;
}
