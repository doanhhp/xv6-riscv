#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PGSIZE 4096
#define NPAGES 10

int main(void)
{
    char *buf = sbrk(NPAGES * PGSIZE);
    if (buf == (char *)-1) {
        printf("TEST: FAIL sbrk\n");
        exit(1);
    }

    printf("=== BEFORE CHECKPOINT ===\n");
    printf("TEST: allocated %d pages (untouched)\n", NPAGES);
    printf("TEST: PID=%d\n", getpid());

    pause(50);

    /* Sau restore */
    printf("\n=== AFTER RESTART ===\n");
    buf[(NPAGES - 1) * PGSIZE] = 0xCC;

    printf("TEST: writing to last page...\n");

    if (buf[(NPAGES - 1) * PGSIZE] == 0xCC)
        printf("TEST: PASS lazy page fault after restore\n");
    else
        printf("TEST: FAIL lazy page fault\n");

    exit(0);
}
