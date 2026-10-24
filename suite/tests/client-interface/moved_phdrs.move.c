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

/* i#2344: a build-time helper for the client.moved_phdrs test.
 *
 *   moved_phdrs.move <in> <out>
 *
 * Copies an ELF64 shared library with its program headers and its dynamic string
 * table moved into a segment of their own at the end of the file, as patchelf 0.9
 * moved them when it added a string.  The headers' file offset is then not their
 * offset from the library's base in memory, and the strings lie where the first
 * mapping of the file, which the loader then replaces with the segments, does not
 * reach.  The section headers still describe the original layout, which stays in
 * the file.
 */

#include <elf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ALIGN_UP(x, a) (((x) + (a) - 1) & ~((Elf64_Addr)(a) - 1))

/* Returns the file offset of virtual address vaddr, or 0 if no segment maps it. */
static Elf64_Off
offset_of(const Elf64_Phdr *phdrs, int num, Elf64_Addr vaddr)
{
    for (int i = 0; i < num; i++) {
        if (phdrs[i].p_type == PT_LOAD && vaddr >= phdrs[i].p_vaddr &&
            vaddr < phdrs[i].p_vaddr + phdrs[i].p_filesz)
            return phdrs[i].p_offset + (vaddr - phdrs[i].p_vaddr);
    }
    return 0;
}

static int
fail(const char *why)
{
    fprintf(stderr, "moved_phdrs.move: %s\n", why);
    return 1;
}

int
main(int argc, char **argv)
{
    if (argc != 3)
        return fail("usage: moved_phdrs.move <in> <out>");
    FILE *in = fopen(argv[1], "rb");
    if (in == NULL)
        return fail("cannot open the input");
    fseek(in, 0, SEEK_END);
    long size = ftell(in);
    fseek(in, 0, SEEK_SET);
    unsigned char *buf = malloc(size);
    if (buf == NULL || fread(buf, 1, size, in) != (size_t)size)
        return fail("cannot read the input");
    fclose(in);
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)buf;
    if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0 ||
        ehdr->e_ident[EI_CLASS] != ELFCLASS64)
        return fail("not an ELF64 file");
    const Elf64_Phdr *old = (const Elf64_Phdr *)(buf + ehdr->e_phoff);
    int num = ehdr->e_phnum;
    Elf64_Addr end = 0;
    Elf64_Dyn *dyn = NULL;
    for (int i = 0; i < num; i++) {
        if (old[i].p_type == PT_LOAD && old[i].p_vaddr + old[i].p_memsz > end)
            end = old[i].p_vaddr + old[i].p_memsz;
        if (old[i].p_type == PT_DYNAMIC)
            dyn = (Elf64_Dyn *)(buf + old[i].p_offset);
    }
    Elf64_Dyn *strtab = NULL;
    Elf64_Xword strsz = 0;
    for (; dyn != NULL && dyn->d_tag != DT_NULL; dyn++) {
        if (dyn->d_tag == DT_STRTAB)
            strtab = dyn;
        else if (dyn->d_tag == DT_STRSZ)
            strsz = dyn->d_un.d_val;
    }
    if (strtab == NULL || strsz == 0)
        return fail("no dynamic string table");
    Elf64_Off strings = offset_of(old, num, strtab->d_un.d_ptr);
    if (strings == 0)
        return fail("no segment maps the dynamic string table");

    /* The new segment, after every other: the headers, then the strings. */
    Elf64_Off seg_offset = ALIGN_UP((Elf64_Off)size, 0x1000);
    Elf64_Addr seg_vaddr = ALIGN_UP(end, 0x1000);
    size_t table = (num + 1) * sizeof(Elf64_Phdr);
    size_t total = seg_offset + table + strsz;
    unsigned char *out_buf = calloc(1, total);
    if (out_buf == NULL)
        return fail("out of memory");
    memcpy(out_buf, buf, size);
    Elf64_Phdr *phdrs = (Elf64_Phdr *)(out_buf + seg_offset);
    memcpy(phdrs, old, num * sizeof(Elf64_Phdr));
    memcpy(out_buf + seg_offset + table, buf + strings, strsz);
    Elf64_Phdr *seg = &phdrs[num];
    seg->p_type = PT_LOAD;
    seg->p_flags = PF_R;
    seg->p_offset = seg_offset;
    seg->p_vaddr = seg->p_paddr = seg_vaddr;
    seg->p_filesz = seg->p_memsz = table + strsz;
    seg->p_align = 0x1000;
    for (int i = 0; i < num; i++) {
        if (phdrs[i].p_type == PT_PHDR) {
            phdrs[i].p_offset = seg_offset;
            phdrs[i].p_vaddr = phdrs[i].p_paddr = seg_vaddr;
            phdrs[i].p_filesz = phdrs[i].p_memsz = table;
        }
    }
    ((Elf64_Dyn *)(out_buf + ((unsigned char *)strtab - buf)))->d_un.d_ptr =
        seg_vaddr + table;
    Elf64_Ehdr *out_ehdr = (Elf64_Ehdr *)out_buf;
    out_ehdr->e_phoff = seg_offset;
    out_ehdr->e_phnum = num + 1;
    FILE *out = fopen(argv[2], "wb");
    if (out == NULL || fwrite(out_buf, 1, total, out) != total)
        return fail("cannot write the output");
    fclose(out);
    return 0;
}
