#include "elf_loader.h"
#include "symbol_table.h"
#include "logger.h"
#include <sys/mman.h>
#include <elf.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>

static void patchSwitchTls(uint32_t* code, size_t count) {
    size_t patched = 0;
    for (size_t i = 0; i < count; ++i) {
        if ((code[i] & 0xFFFFFFE0) == 0xD53BD060) {
            uint32_t reg = code[i] & 0x1F;
            code[i] = 0xD53BD040 | reg;
            patched++;
        }
    }
    __builtin___clear_cache(reinterpret_cast<char*>(code), reinterpret_cast<char*>(code + count));
    writeLog("[PATCHER] %zu instruksi TLS ARM64 berhasil di-patch.", patched);
}

void* loadSwitchElf(const char* elfPath) {
    FILE* f = fopen(elfPath, "rb");
    if (!f) {
        writeLog("[ERROR] File tidak ditemukan: %s", elfPath);
        return nullptr;
    }

    fseek(f, 0, SEEK_END);
    size_t fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    std::vector<uint8_t> buf(fileSize);
    fread(buf.data(), 1, fileSize, f);
    fclose(f);

    auto* ehdr = reinterpret_cast<Elf64_Ehdr*>(buf.data());
    if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0) {
        writeLog("[ERROR] Format ELF tidak valid!");
        return nullptr;
    }

    auto* phdrs = reinterpret_cast<Elf64_Phdr*>(buf.data() + ehdr->e_phoff);
    uint64_t minVaddr = UINT64_MAX, maxVaddr = 0;

    for (int i = 0; i < ehdr->e_phnum; ++i) {
        if (phdrs[i].p_type == PT_LOAD) {
            if (phdrs[i].p_vaddr < minVaddr) minVaddr = phdrs[i].p_vaddr;
            if (phdrs[i].p_vaddr + phdrs[i].p_memsz > maxVaddr)
                maxVaddr = phdrs[i].p_vaddr + phdrs[i].p_memsz;
        }
    }

    size_t totalMem = maxVaddr - minVaddr;
    writeLog("[ELF] Alokasi memori kode game: %.2f MB", totalMem / (1024.0 * 1024.0));

    uint8_t* base = reinterpret_cast<uint8_t*>(
        mmap(nullptr, totalMem, PROT_READ | PROT_WRITE | PROT_EXEC,
             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)
    );
    if (base == MAP_FAILED) {
        writeLog("[ERROR] Gagal mmap memori!");
        return nullptr;
    }

    for (int i = 0; i < ehdr->e_phnum; ++i) {
        if (phdrs[i].p_type == PT_LOAD) {
            uint8_t* dest = base + (phdrs[i].p_vaddr - minVaddr);
            memcpy(dest, buf.data() + phdrs[i].p_offset, phdrs[i].p_filesz);
            if (phdrs[i].p_flags & PF_X) {
                patchSwitchTls(reinterpret_cast<uint32_t*>(dest), phdrs[i].p_filesz / 4);
            }
        }
    }

    auto* shdrs = reinterpret_cast<Elf64_Shdr*>(buf.data() + ehdr->e_shoff);
    for (int i = 0; i < ehdr->e_shnum; ++i) {
        if (shdrs[i].sh_type == SHT_RELA) {
            auto* relas = reinterpret_cast<Elf64_Rela*>(buf.data() + shdrs[i].sh_offset);
            size_t count = shdrs[i].sh_size / sizeof(Elf64_Rela);
            auto& symShdr = shdrs[shdrs[i].sh_link];
            auto* syms = reinterpret_cast<Elf64_Sym*>(buf.data() + symShdr.sh_offset);
            const char* strtab = reinterpret_cast<const char*>(buf.data() + shdrs[symShdr.sh_link].sh_offset);

            for (size_t r = 0; r < count; ++r) {
                uint64_t* target = reinterpret_cast<uint64_t*>(base + (relas[r].r_offset - minVaddr));
                uint32_t type = ELF64_R_TYPE(relas[r].r_info);
                uint32_t symIdx = ELF64_R_SYM(relas[r].r_info);

                if (type == R_AARCH64_RELATIVE) {
                    *target = reinterpret_cast<uint64_t>(base) + relas[r].r_addend;
                } else if (type == R_AARCH64_GLOB_DAT || type == R_AARCH64_JUMP_SLOT || type == R_AARCH64_ABS64) {
                    if (symIdx != 0) {
                        Elf64_Sym& sym = syms[symIdx];
                        if (sym.st_shndx != SHN_UNDEF) {
                            *target = reinterpret_cast<uint64_t>(base + (sym.st_value - minVaddr)) + relas[r].r_addend;
                        } else {
                            *target = reinterpret_cast<uint64_t>(resolveSymbol(strtab + sym.st_name));
                        }
                    }
                }
            }
        }
    }

    void* entry = base + (ehdr->e_entry - minVaddr);
    buf.clear();
    buf.shrink_to_fit();
    return entry;
}
