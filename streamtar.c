#include <errno.h> /* errno */
#include <fcntl.h> /* open */
#include <stdio.h> /* printf */
#include <stdlib.h> /* exit */
#include <string.h> /* memset */
#include <sys/file.h> /* flock */
#include <time.h> /* time */
#include <unistd.h> /* lseek */

#include <libtar.h>

// IF THIS SOFTWARE IS EXPANDED, FIRST APPLY DRY:
// - testing the result of a call (uses 'perror' and 'exit')

off_t seek_or_exit(TAR *tar, off_t pos, int whence)
{
    off_t pos2 = lseek(tar->fd, pos, whence);
    if (pos2 == -1 || (pos && pos2 != pos)) {
        perror("lseek");
        exit(errno);
    }
    return pos2;
}

void flock_or_exit(TAR *tar, int lock)
{
    int r = flock(tar->fd, lock ? LOCK_SH : LOCK_UN);
    if (r == -1) {
        perror(lock ? "flock" : "funlock");
        exit(errno);
    }
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

    flock_or_exit(tar, 1);
    /* header is updated first, so seeking past the end on next open will make a valid file with hole. */
    write_or_exit(tar, header_offset, SEEK_SET, &tar->th_buf, T_BLOCKSIZE);
    write_or_exit(tar, 0, SEEK_END, buffer, length);
    flock_or_exit(tar, 0);
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

    flock_or_exit(tar, 1);
    write_or_exit(tar, hpos, SEEK_SET, &tar->th_buf, T_BLOCKSIZE);
    flock_or_exit(tar, 0);

    ssize_t total, offset = 0;
    time_t mtime = th_get_mtime(tar);
    char buffer[T_BLOCKSIZE * 1024];
    while ((total = read(0, buffer + offset, sizeof(buffer) - offset)) > 0) {
        mtime = time(0);
        total += offset;
        offset = total % T_BLOCKSIZE;
        size_t size = total - offset;
        append(tar, hpos, mtime, buffer, size);
        memcpy(buffer, buffer + size, offset);
    }

    /* write final chunk buffer */
    /* this isn't written for terminations midway because the only midway termination is a write failure. */

    append(tar, hpos, mtime, buffer, offset);

    if (errno) {
        perror("read");
    }

    th_print_long_ls(tar);

    return errno;
}
