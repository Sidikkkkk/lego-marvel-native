#include "elf_loader.h"
#include "symbol_table.h"
#include "logger.h"
#include <sys/mman.h>
#include <signal.h>
#include <ucontext.h>
#include <elf.h>
#include <unistd.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>

static uint8_t* g_ElfBase = nullptr;
static uint8_t* g_SwitchTls = nullptr;
static uint64_t g_AndroidTls = 0;
static std::vector<uint64_t> g_InitArrayFns;
static void* g_NnMainPtr = nullptr;

static inline uint64_t getTpidrEl0() {
    uint64_t val;
    asm volatile("mrs %0, tpidr_el0" : "=r"(val));
    return val;
}

static inline void setTpidrEl0(uint64_t val) {
    asm volatile("msr tpidr_el0, %0" : : "r"(val));
}

static void nativeSignalHandler(int sig, siginfo_t* info, void* ucontext) {
    uint64_t currentTls = getTpidrEl0();
    if (g_AndroidTls != 0) setTpidrEl0(g_AndroidTls);

    auto* uc = reinterpret_cast<ucontext_t*>(ucontext);
    uint64_t pc = uc->uc_mcontext.pc;
    uint64_t offset = (g_ElfBase && pc >= reinterpret_cast<uint64_t>(g_ElfBase))
                      ? (pc - reinterpret_cast<uint64_t>(g_ElfBase)) : 0;

    if (sig == SIGTRAP) {
        uint32_t inst = *reinterpret_cast<uint32_t*>(pc);
        if ((inst & 0xFFE0001F) == 0xD4200000) {
            uint16_t svcId = (inst >> 5) & 0xFFFF;
            writeLog("[SVC] Syscall Horizon OS: 0x%02X (Offset ELF: 0x%llx) | x0=0x%llx, x1=0x%llx",
                     svcId, (unsigned long long)offset,
                     (unsigned long long)uc->uc_mcontext.regs[0],
                     (unsigned long long)uc->uc_mcontext.regs[1]);

            uc->uc_mcontext.regs[0] = 0;
            uc->uc_mcontext.pc += 4;
            setTpidrEl0(currentTls);
            return;
        }
    }

    writeLog("[CRASH] Signal %d (%s) di PC=0x%llx (Offset ELF: 0x%llx) | Fault Addr=%p",
             sig, strsignal(sig), (unsigned long long)pc, (unsigned long long)offset, info->si_addr);
    writeLog("[CRASH] x0=0x%llx x1=0x%llx x2=0x%llx LR(x30)=0x%llx",
             (unsigned long long)uc->uc_mcontext.regs[0],
             (unsigned long long)uc->uc_mcontext.regs[1],
             (unsigned long long)uc->uc_mcontext.regs[2],
             (unsigned long long)uc->uc_mcontext.regs[30]);
    closeLogger();
    _exit(1);
}

void installCrashAndSvcHandlers() {
    g_AndroidTls = getTpidrEl0();

    g_SwitchTls = reinterpret_cast<uint8_t*>(
        mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)
    );
    memset(g_SwitchTls, 0, 4096);
    *reinterpret_cast<uint64_t*>(g_SwitchTls + 0x1F8) = reinterpret_cast<uint64_t>(g_SwitchTls + 0x200);

    struct sigaction sa{};
    sa.sa_sigaction = nativeSignalHandler;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGTRAP, &sa, nullptr);
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGILL,  &sa, nullptr);
    sigaction(SIGBUS,  &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
}

static void patchSwitchInstructions(uint32_t* code, size_t count) {
    size_t patchedTls = 0;
    size_t patchedSvc = 0;

    for (size_t i = 0; i < count; ++i) {
        uint32_t inst = code[i];
        if ((inst & 0xFFFFFFE0) == 0xD53BD060) {
            uint32_t reg = inst & 0x1F;
            code[i] = 0xD53BD040 | reg;
            patchedTls++;
        } else if ((inst & 0xFFE0001F) == 0xD4000001) {
            uint32_t imm16 = (inst >> 5) & 0xFFFF;
            code[i] = 0xD4200000 | (imm16 << 5);
            patchedSvc++;
        }
    }

    __builtin___clear_cache(reinterpret_cast<char*>(code), reinterpret_cast<char*>(code + count));
    writeLog("[PATCHER] Di-patch: %zu instruksi TLS & %zu instruksi SVC Switch.", patchedTls, patchedSvc);
}

void* loadSwitchElf(const char* elfPath) {
    g_InitArrayFns.clear();
    g_NnMainPtr = nullptr;

    FILE* f = fopen(elfPath, "rb");
    if (!f) {
        writeLog("[ERROR] File tidak ditemukan atau izin ditolak: %s", elfPath);
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
    g_ElfBase = base;

    for (int i = 0; i < ehdr->e_phnum; ++i) {
        if (phdrs[i].p_type == PT_LOAD) {
            uint8_t* dest = base + (phdrs[i].p_vaddr - minVaddr);
            memcpy(dest, buf.data() + phdrs[i].p_offset, phdrs[i].p_filesz);
            if (i == 0 || (phdrs[i].p_flags & PF_X)) {
                patchSwitchInstructions(reinterpret_cast<uint32_t*>(dest), phdrs[i].p_filesz / 4);
            }
        }
    }

    auto* words = reinterpret_cast<uint32_t*>(base);
    writeLog("[ELF] Header 0x0: [0x%08x, 0x%08x, 0x%08x, 0x%08x]",
             words[0], words[1], words[2], words[3]);

    auto* shdrs = reinterpret_cast<Elf64_Shdr*>(buf.data() + ehdr->e_shoff);

    // 1. Proses relokasi pointer dan simbol eksternal
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

    // 2. Cari simbol entry point game asli (nnMain / nninitStartup)
    for (int i = 0; i < ehdr->e_shnum; ++i) {
        if (shdrs[i].sh_type == SHT_DYNSYM || shdrs[i].sh_type == SHT_SYMTAB) {
            auto* syms = reinterpret_cast<Elf64_Sym*>(buf.data() + shdrs[i].sh_offset);
            size_t symCount = shdrs[i].sh_size / sizeof(Elf64_Sym);
            const char* strtab = reinterpret_cast<const char*>(buf.data() + shdrs[shdrs[i].sh_link].sh_offset);

            for (size_t s = 0; s < symCount; ++s) {
                if (syms[s].st_shndx != SHN_UNDEF && syms[s].st_name != 0) {
                    const char* symName = strtab + syms[s].st_name;
                    if (strcmp(symName, "nnMain") == 0) {
                        g_NnMainPtr = base + (syms[s].st_value - minVaddr);
                        writeLog("[ELF] Ditemukan simbol nnMain di offset 0x%llx",
                                 (unsigned long long)(syms[s].st_value - minVaddr));
                    } else if (strcmp(symName, "nninitStartup") == 0 && !g_NnMainPtr) {
                        g_NnMainPtr = base + (syms[s].st_value - minVaddr);
                        writeLog("[ELF] Ditemukan simbol nninitStartup di offset 0x%llx",
                                 (unsigned long long)(syms[s].st_value - minVaddr));
                    }
                }
            }
        }
    }

    // 3. Kumpulkan fungsi konstruktor C++ (.init_array) setelah relokasi selesai
    for (int i = 0; i < ehdr->e_shnum; ++i) {
        if (shdrs[i].sh_type == SHT_INIT_ARRAY) {
            uint64_t* arr = reinterpret_cast<uint64_t*>(base + (shdrs[i].sh_addr - minVaddr));
            size_t count = shdrs[i].sh_size / sizeof(uint64_t);
            for (size_t k = 0; k < count; ++k) {
                if (arr[k] != 0 && arr[k] != UINT64_MAX) {
                    g_InitArrayFns.push_back(arr[k]);
                }
            }
            writeLog("[ELF] Ditemukan %zu konstruktor C++ di .init_array", g_InitArrayFns.size());
        }
    }

    void* entry = g_NnMainPtr ? g_NnMainPtr : (base + (ehdr->e_entry - minVaddr));
    buf.clear();
    buf.shrink_to_fit();
    return entry;
}

void jumpToSwitchEntry(void* entryPoint) {
    using Fn = void (*)();

    if (!g_InitArrayFns.empty()) {
        writeLog("[BOOT] Menjalankan %zu konstruktor global C++ (.init_array)...", g_InitArrayFns.size());
        setTpidrEl0(reinterpret_cast<uint64_t>(g_SwitchTls));
        for (size_t i = 0; i < g_InitArrayFns.size(); ++i) {
            reinterpret_cast<Fn>(g_InitArrayFns[i])();
        }
        setTpidrEl0(g_AndroidTls);
        writeLog("[BOOT] Semua konstruktor .init_array selesai.");
    }

    uint64_t offset = reinterpret_cast<uint64_t>(entryPoint) - reinterpret_cast<uint64_t>(g_ElfBase);
    writeLog("[BOOT] Memanggil nnMain() di %p (Offset ELF: 0x%llx)...",
             entryPoint, (unsigned long long)offset);

    setTpidrEl0(reinterpret_cast<uint64_t>(g_SwitchTls));
    reinterpret_cast<Fn>(entryPoint)();
    setTpidrEl0(g_AndroidTls);
    writeLog("[BOOT] nnMain() selesai dieksekusi.");
}
