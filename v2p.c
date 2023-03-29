#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#define GB (1UL << 30)
#define ROW_SIZE (1UL << 12) // 4KB
#define BANK_SIZE (1UL << 15) // 32KB
#define RANK_SIZE (1UL << 17) // 128KB

//32 bit addr space
// col: 3-11 - shift 3 bits
// bank: 12-14 shift 12 bits
// row: 15-30 shift 15 bits
#define COL_MASK 0x1FF
#define BANK_MASK 0x7
#define ROW_MASK 0x1FFFF
#define COL_SHIFT 3
#define BANK_SHIFT 12
#define ROW_SHIFT 15

ssize_t readn(int fd, void *buf, size_t count)
{
    char *cbuf = buf;
    ssize_t nr, n = 0;

    while (n < count) {
        nr = read(fd, &cbuf[n], count - n);
        if (nr == 0) {
            //EOF
            break;
        } else if (nr == -1) {
            if (errno == -EINTR) {
                //retry
                continue;
            } else {
                //error
                return -1;
            }
        }
        n += nr;
    }

    return n;
}

/**
 * @return -1: error, -2: not present, other: physical address
 */
uint64_t virt_to_phys(int fd, uint64_t virtaddr)
{
    int pagesize;
    uint64_t tbloff, tblen, pageaddr, physaddr;
    off_t offset;
    ssize_t nr;

    uint64_t tbl_present;
    uint64_t tbl_swapped;
    //uint64_t tbl_shared;
    //uint64_t tbl_pte_dirty;
    uint64_t tbl_swap_offset;
    //uint64_t tbl_swap_type;

    //1PAGE = typically 4KB, 1entry = 8bytes
    pagesize = (int)sysconf(_SC_PAGESIZE);
    //see: linux/Documentation/vm/pagemap.txt
    tbloff = virtaddr / pagesize * sizeof(uint64_t);

    offset = lseek(fd, tbloff, SEEK_SET);
    if (offset == (off_t)-1) {
        perror("lseek");
        return -1;
    }
    if (offset != tbloff) {
        fprintf(stderr, "Cannot found virt:0x%08llx, "
                        "tblent:0x%08llx, returned offset:0x%08llx.\n",
                (long long)virtaddr, (long long)tbloff,
                (long long)offset);
        return -1;
    }

    nr = readn(fd, &tblen, sizeof(uint64_t));
    if (nr == -1 || nr < sizeof(uint64_t)) {
        fprintf(stderr, "Cannot found virt:0x%08llx, "
                        "tblent:0x%08llx, returned offset:0x%08llx, "
                        "returned size:0x%08x.\n",
                (long long)virtaddr, (long long)tbloff,
                (long long)offset, (int)nr);
        return -1;
    }

    tbl_present   = (tblen >> 63) & 0x1;
    tbl_swapped   = (tblen >> 62) & 0x1;
    //tbl_shared    = (tblen >> 61) & 0x1;
    //tbl_pte_dirty = (tblen >> 55) & 0x1;
    if (!tbl_swapped) {
        tbl_swap_offset = (tblen >> 0) & 0x7fffffffffffffULL;
    } else {
        tbl_swap_offset = (tblen >> 5) & 0x3ffffffffffffULL;
        //tbl_swap_type = (tblen >> 0) & 0x1f;
    }

    pageaddr = tbl_swap_offset * pagesize;
    physaddr = (uint64_t)pageaddr | (virtaddr & (pagesize - 1));

    if (tbl_present) {
        return physaddr;
    } else {
        return -2;
    }
}

int main(int argc, char *argv[])
{
    //malloc reserves memory in the heap, could use stack???
    char* buffer = (char *)malloc(GB);
    int fd = -1;
    //int pagesize;
    //uint64_t virtaddr, areasize, Basephysaddr, physaddr, v;
    int result = -1;

    fd = open("/proc/self/pagemap", O_RDONLY);
    if (fd == -1) {
        perror("open");
        goto err_out;
    }

    for (size_t i = 0; i < GB; i += ROW_SIZE) {
        printf("Trying Address 0x%lux\n", (uint64_t)&buffer[i]);
        uint64_t address = virt_to_phys(fd, (uint64_t)&buffer[i]);
        if (address == -1) {
            printf(" Base virt:0x%08lux, (%s)\n",
                   (uint64_t)&buffer, "not valid virtual address\n");
            break;
        }
        uint64_t row = (address >> 15) & ROW_MASK;
        uint64_t bank = (address >> 12) & BANK_MASK;
        uint64_t column = (address >> 3) & COL_MASK;
        printf("Address 0x%lx: Row %lu, Bank %lu, Column %lu\n", address, row, bank, column);
    }

    /*
    virtaddr = (long long)*baseAddr;
    pagesize = (int)sysconf(_SC_PAGESIZE);
    virtaddr &= ~(pagesize - 1);
    Basephysaddr = virt_to_phys(fd, virtaddr);
    if (Basephysaddr == -1) {
        printf(" Base virt:0x%08llx, (%s)\n",
               (long long)virtaddr, "not valid virtual address");
        break;
    } else if (Basephysaddr == -2) {
        printf(" Base virt:0x%08llx, phys:(%s)\n",
               (long long)virtaddr, "not present");
    } else {
        printf(" Base virt:0x%08llx, phys:0x%08llx\n",
               (long long)virtaddr, (long long)Basephysaddr);
    }
    */

    result = 0;

    err_out:
    if (fd != -1) {
        close(fd);
        fd = -1;
    }
    free(buffer);
    return result;
}