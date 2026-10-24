/* **********************************************************
 * Copyright (c) 2026 Jonas Karg.  All rights reserved.
 * **********************************************************/

/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * * Neither the name of the copyright holder nor the names of its contributors
 *   may be used to endorse or promote products derived from this software
 *   without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/* A writable code region that also holds data the program writes, from code
 * outside the region: as a library whose segments are all writable and
 * executable keeps its variables among its code.  DR must keep the code it runs
 * from the region consistent -- code written after it ran, and code first run
 * after the data was written, must be what runs -- without flushing the region's
 * code at every write to the data.
 */

#include "tools.h"

#define PAGES 16
#define ITERATIONS 10000

typedef int (*func_t)(void);

/* mov eax, value; ret */
static void
write_func(char *at, int value)
{
    at[0] = (char)0xb8;
    memcpy(at + 1, &value, sizeof(value));
    at[5] = (char)0xc3;
}

int
main(void)
{
    INIT();
    char *region = allocate_mem(PAGES * PAGE_SIZE, ALLOW_EXEC | ALLOW_WRITE | ALLOW_READ);
    if (region == NULL) {
        print("cannot allocate\n");
        return 1;
    }
    func_t first = (func_t)region;
    func_t later = (func_t)(region + 4 * PAGE_SIZE);
    volatile int *data = (volatile int *)(region + (PAGES - 1) * PAGE_SIZE);
    write_func(region, 1);
    int sum = 0;
    for (int i = 0; i < ITERATIONS; i++) {
        sum += first();
        *data = i;
    }
    print("ran %d times, data %d\n", sum, *data);
    /* Code that already ran, rewritten from outside the region. */
    write_func(region, 2);
    print("rewritten: %d\n", first());
    /* Code on a page that had not run yet. */
    write_func(region + 4 * PAGE_SIZE, 3);
    print("first run of a new page: %d\n", later());
    *data = -1;
    write_func(region + 4 * PAGE_SIZE, 4);
    print("rewritten after a data write: %d\n", later());
    free_mem(region, PAGES * PAGE_SIZE);
    return 0;
}
