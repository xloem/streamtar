#include <errno.h> /* errno */
#include <fcntl.h> /* open */
#include <stdio.h> /* printf */
#include <stdlib.h> /* exit */
#include <string.h> /* memset */
#include <time.h> /* time */
#include <unistd.h> /* lseek */

#include <libtar.h>

// IF THIS SOFTWARE IS EXPANDED, FIRST THE TWO REPEATED CODE BLOCKS SHOULD BE CONSOLIDATED, IF KEPT.
// 1. testing the result of a call (uses 'perror' and 'exit')
// 2. writing a new block of data with updated size (uses 'write_or_exit' twice)

off_t seek_or_exit(TAR *tar, off_t pos, int whence)
{
    off_t pos2 = lseek(tar->fd, pos, whence);
    if (pos2 == -1 || (pos && pos2 != pos)) {
        perror("lseek");
        exit(errno);
    }
    return pos2;
}

void write_or_exit(TAR *tar, off_t pos, int whence, void const * buffer, size_t count)
{
    int r;
    seek_or_exit(tar, pos, whence);
    r = write(tar->fd, buffer, count);
    if (r != count) {
        perror("write");
        exit(errno);
    }
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
    int fd = open(outer_fn, O_RDWR | O_CREAT, 0600);
    if (fd == -1) {
        perror("open");
        exit(errno);
    }
    int r = tar_fdopen(&tar, fd, outer_fn, NULL, 0, 0600, TAR_VERBOSE | TAR_NOOVERWRITE);
    if (r == -1) {
        perror("tar_fdopen");
        exit(errno);
    }
    off_t hpos = 0;

    /* read existing records to seek to correct end for short writes */
    while ((r = th_read(tar)) == 0) {
        if (tar->th_buf.name[0] == 0) {
            // EOF indicator
            break;
        }
        size_t size = th_get_size(tar);
        size_t extra = size % T_BLOCKSIZE;
        size_t total = extra > 0 ? size - extra + T_BLOCKSIZE : size;
        hpos = lseek(fd, total, SEEK_CUR);
        if (hpos == -1) {
            perror("lseek");
            exit(errno);
        }
    }

    if (r == -1) {
        perror("th_read");
        exit(errno);
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

    write_or_exit(tar, hpos, SEEK_SET, &tar->th_buf, T_BLOCKSIZE);

    ssize_t total, size, offset = 0;
    char buffer[T_BLOCKSIZE * 1024];
    while ((total = read(0, buffer + offset, sizeof(buffer) - offset)) > 0) {
        th_set_mtime(tar, time(0)); // last modification time
        total += offset;
        offset = total % T_BLOCKSIZE;
        size = total - offset;
        th_set_size(tar, th_get_size(tar) + size); // size
        int_to_oct(th_crc_calc(tar), tar->th_buf.chksum, 8); // chksum

        /* header is updated first, so seeking past the end on next open will make a valid file with hole. */
        write_or_exit(tar, hpos, SEEK_SET, &tar->th_buf, T_BLOCKSIZE);

        write_or_exit(tar, 0, SEEK_END, buffer, size);
        memcpy(buffer, &buffer[size], offset);
    }

    /* write padded buffer */
    memset(buffer + offset, 0, T_BLOCKSIZE - offset);

    th_set_size(tar, th_get_size(tar) + offset); // size
    int_to_oct(th_crc_calc(tar), tar->th_buf.chksum, 8); // chksum
    /* header is updated first, so seeking past the end on next open will make a valid file with hole. */
    write_or_exit(tar, hpos, SEEK_SET, &tar->th_buf, T_BLOCKSIZE);
    write_or_exit(tar, 0, SEEK_END, buffer, T_BLOCKSIZE);

    if (errno) {
        perror("read");
    }
    th_print_long_ls(tar);

    return errno;
}
