#include "y86sim.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

Y_data *y86_new() {
    return mmap(
        0, sizeof(Y_data),
        PROT_READ | PROT_WRITE | PROT_EXEC,
        MAP_PRIVATE | MAP_ANONYMOUS,
        -1, 0
    );
}

const Y_word y_static_num[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

#define YX(data) {y86_push_x(y, data);}
#define YXW(data) {y86_push_x_word(y, data);}
#define YXA(data) {y86_push_x_addr(y, data);}

void y86_push_x(Y_data *y, Y_char value) {
    if (y->x_end < &(y->x_inst[Y_X_INST_SIZE])) {
        *(y->x_end) = value;
        y->x_end++;
    } else {
        fprintf(stderr, "Too large compiled instruction size (char: 0x%x)\n", value);
        longjmp(y->jmp, ys_ccf);
    }
}

void y86_push_x_word(Y_data *y, Y_word value) {
    if (y->x_end + sizeof(Y_word) <= &(y->x_inst[Y_X_INST_SIZE])) {
        IO_WORD(y->x_end) = value;
        y->x_end += sizeof(Y_word);
    } else {
        fprintf(stderr, "Too large compiled instruction size (word: 0x%x)\n", value);
        longjmp(y->jmp, ys_ccf);
    }
}

void y86_push_x_addr(Y_data *y, Y_addr value) {
    if (y->x_end + sizeof(Y_addr) <= &(y->x_inst[Y_X_INST_SIZE])) {
        IO_ADDR(y->x_end) = value;
        y->x_end += sizeof(Y_addr);
    } else {
        fprintf(stderr, "Too large compiled instruction size (addr: 0x%x)\n", (Y_word) value);
        longjmp(y->jmp, ys_ccf);
    }
}

void y86_link_x_map(Y_data *y, Y_word pos) {
    if (pos < Y_Y_INST_SIZE) {
        y->x_map[pos] = y->x_end;
    } else {
        fprintf(stderr, "Too large y86 instruction size\n");
        longjmp(y->jmp, ys_ccf);
    }
}

void y86_gen_enesp(Y_data *y) {
    YX(0x8D) YX(0xA4) YX(0x24) YXA(&y->mem[0]) // leal y->mem(%esp), %esp
}

void y86_gen_enesp_d4(Y_data *y) {
    YX(0x8D) YX(0xA4) YX(0x24) YXW((Y_word) &y->mem[0] - 4) // leal y->mem-4(%esp), %esp
}

void y86_gen_deesp(Y_data *y) {
    YX(0x8D) YX(0xA4) YX(0x24) YXW(- (Y_word) &y->mem[0]) // leal -y->mem(%esp), %esp
}

void y86_gen_enter(Y_data *y) {
    YX(0x0F) YX(0x7E) YX(0xCC) // movd %mm1, %esp
    y86_gen_enesp(y);
}

void y86_gen_leave(Y_data *y) {
    y86_gen_deesp(y);
    YX(0x0F) YX(0x6E) YX(0xCC) // movd %esp, %mm1
    YX(0x0F) YX(0x7E) YX(0xD4) // movd %mm2, %esp
    YX(0xFF) YX(0x14) YX(0x24) // call (%esp)
}

void y86_gen_stat(Y_data *y, Y_stat stat) {
    YX(0x0F) YX(0x6E) YX(0x3D) YXA((Y_addr) &(y_static_num[stat])) // movd stat, %mm7
}

void y86_gen_return(Y_data *y, Y_stat stat) {
    y86_gen_stat(y, stat);
    y86_gen_leave(y);
}

void y86_gen_raw_jmp(Y_data *y, Y_addr value) {
    YX(0xFF) YX(0x25) YXA(value) // jmp *value
}

void y86_gen_raw_call(Y_data *y, Y_addr value) {
    YX(0xFF) YX(0x15) YXA(value) // call *value
}

Y_char y86_x_regbyte_C(Y_reg_id ra, Y_reg_id rb) {
    return 0xC0 | (ra << 3) | rb;
}

Y_char y86_x_regbyte_8(Y_reg_id ra, Y_reg_id rb) {
    return 0x80 | (ra << 3) | rb;
}

void y86_gen_x(Y_data *y, Y_inst op, Y_reg_id ra, Y_reg_id rb, Y_word val) {
    Y_word jmp_skip = 6;

    // Always: ra, rb >= 0
    switch (op) {
        case yi_halt:
            y86_gen_return(y, ys_hlt);
            break;
        case yi_nop:
            // Nothing
            break;
        case yi_rrmovl:
        case yi_cmovle:
        case yi_cmovl:
        case yi_cmove:
        case yi_cmovne:
        case yi_cmovge:
        case yi_cmovg:
            if (ra < yr_cnt && rb < yr_cnt) {
                if ((ra == yri_esp) != (rb == yri_esp)) {
                    y86_gen_deesp(y);
                }

                switch (op) {
                    case yi_rrmovl:
                        YX(0x89) YX(y86_x_regbyte_C(ra, rb)) // movl ...
                        break;
                    case yi_cmovle:
                        YX(0x0F) YX(0x4E) YX(y86_x_regbyte_C(rb, ra)) // cmovle ...
                        break;
                    case yi_cmovl:
                        YX(0x0F) YX(0x4C) YX(y86_x_regbyte_C(rb, ra)) // cmovl ...
                        break;
                    case yi_cmove:
                        YX(0x0F) YX(0x44) YX(y86_x_regbyte_C(rb, ra)) // cmove ...
                        break;
                    case yi_cmovne:
                        YX(0x0F) YX(0x45) YX(y86_x_regbyte_C(rb, ra)) // cmovne ...
                        break;
                    case yi_cmovge:
                        YX(0x0F) YX(0x4D) YX(y86_x_regbyte_C(rb, ra)) // cmovge ...
                        break;
                    case yi_cmovg:
                        YX(0x0F) YX(0x4F) YX(y86_x_regbyte_C(rb, ra)) // cmovg ...
                        break;
                    default:
                        // Impossible
                        fprintf(stderr, "Internal bug!\n");
                        longjmp(y->jmp, ys_ccf);
                        break;
                }

                if ((ra == yri_esp) != (rb == yri_esp)) {
                    y86_gen_enesp(y);
                }
            } else {
                y86_gen_return(y, ys_ins);
            }
            break;
        case yi_irmovl:
            if (ra == yr_nil && rb < yr_cnt) {
                YX(0xB8 + rb) YXW(rb == yri_esp ? val + (Y_word) &y->mem[0] : val) // movl ...

                // No longer necessary
                // if (rb == yri_esp) {
                //     y86_gen_enesp(y);
                // }
            } else {
                y86_gen_return(y, ys_ins);
            }
            break;
        case yi_rmmovl:
            if (ra < yr_cnt && rb < yr_cnt) {
                if (ra == yri_esp) {
                    y86_gen_deesp(y);
                }

                YX(0x89) YX(y86_x_regbyte_8(ra, rb)) // movl ...
                if (rb == yri_esp) YX(0x24) // Extra byte for %esp
                YXA(&(y->mem[val]))

                if (ra == yri_esp) {
                    y86_gen_enesp(y);
                }
            } else {
                y86_gen_return(y, ys_ins);
            }
            break;
        case yi_mrmovl:
            if (ra < yr_cnt && rb < yr_cnt) {
                YX(0x8B) YX(y86_x_regbyte_8(ra, rb)) // movl ...
                if (rb == yri_esp) YX(0x24) // Extra byte for %esp
                YXA(&(y->mem[val]))

                if (ra == yri_esp) {
                    y86_gen_enesp(y);
                }
            } else {
                y86_gen_return(y, ys_ins);
            }
            break;
        case yi_addl:
        case yi_subl:
        case yi_andl:
        case yi_xorl:
            if (ra < yr_cnt && rb < yr_cnt) {
                switch (op) {
                    case yi_addl:
                        if (ra == yri_esp || rb == yri_esp) {
                            y86_gen_deesp(y);
                        }

                        YX(0x01) YX(y86_x_regbyte_C(ra, rb)) // addl ...

                        if (ra == yri_esp || rb == yri_esp) {
                            y86_gen_enesp(y);
                        }
                        break;
                    case yi_subl:
                        if ((ra == yri_esp) != (rb == yri_esp)) {
                            y86_gen_deesp(y);
                        }

                        YX(0x29) YX(y86_x_regbyte_C(ra, rb)) // subl ...

                        if ((ra == yri_esp) != (rb == yri_esp)) {
                            y86_gen_enesp(y);
                        }
                        break;
                    case yi_andl:
                        if (ra == yri_esp || rb == yri_esp) {
                            y86_gen_deesp(y);
                        }

                        YX(0x21) YX(y86_x_regbyte_C(ra, rb)) // andl ...

                        if (ra == yri_esp || rb == yri_esp) {
                            y86_gen_enesp(y);
                        }
                        break;
                    case yi_xorl:
                        if ((ra == yri_esp) != (rb == yri_esp)) {
                            y86_gen_deesp(y);
                        }

                        YX(0x31) YX(y86_x_regbyte_C(ra, rb)) // xorl ...

                        if ((ra == yri_esp) != (rb == yri_esp)) {
                            y86_gen_enesp(y);
                        }
                        break;
                    default:
                        // Impossible
                        fprintf(stderr, "Internal bug!\n");
                        longjmp(y->jmp, ys_ccf);
                        break;
                }
            } else {
                y86_gen_return(y, ys_ins);
            }
            break;
        case yi_jmp:
        case yi_jle:
        case yi_jl:
        case yi_je:
        case yi_jne:
        case yi_jge:
        case yi_jg:
            if (val >= 0 && val < Y_Y_INST_SIZE) {
                switch (op) {
                    case yi_jmp:
                        break;
                    case yi_jle:
                        YX(0x7F) YX(jmp_skip) // jg after
                        break;
                    case yi_jl:
                        YX(0x7D) YX(jmp_skip) // jge after
                        break;
                    case yi_je:
                        YX(0x75) YX(jmp_skip) // jne after
                        break;
                    case yi_jne:
                        YX(0x74) YX(jmp_skip) // je after
                        break;
                    case yi_jge:
                        YX(0x7C) YX(jmp_skip) // jl after
                        break;
                    case yi_jg:
                        YX(0x7E) YX(jmp_skip) // jle after
                        break;
                    default:
                        // Impossible
                        fprintf(stderr, "Internal bug!\n");
                        longjmp(y->jmp, ys_ccf);
                        break;
                }

                if (!y->x_map[val]) {
                    y->x_map[val] = Y_BAD_ADDR;
                }
                y86_gen_raw_jmp(y, (Y_addr) &(y->x_map[val]));
            } else {
                y86_gen_return(y, ys_adp);
            }
            break;
        case yi_call:
            if (val >= 0 && val < Y_Y_INST_SIZE) {
                if (!y->x_map[val]) {
                    y->x_map[val] = Y_BAD_ADDR;
                }
                y86_gen_raw_call(y, (Y_addr) &(y->x_map[val]));
            } else {
                y86_gen_return(y, ys_adp);
            }
            break;
        case yi_ret:
            YX(0xC3) // ret

            break;
        case yi_pushl:
            if (ra < yr_cnt && rb == yr_nil) {
                if (ra == yri_esp) {
                    y86_gen_deesp(y);

                    YX(0x89) YX(y86_x_regbyte_8(ra, yri_esp)) // movl %ra, offset-4(%esp)
                    YX(0x24) // Extra byte for %esp
                    YXA(&(y->mem[0]) - 4)

                    y86_gen_enesp_d4(y);
                    // YX(0x8D) YX(0x64) YX(0x24) YX(0xFC) // leal -4(%esp), %esp
                } else {
                    YX(0x50 + ra) // pushl %ra
                }
            } else {
                y86_gen_return(y, ys_ins);
            }
            break;
        case yi_popl:
            if (ra < yr_cnt && rb == yr_nil) {
                YX(0x58 + ra) // popl %ra

                if (ra == yri_esp) {
                    y86_gen_enesp(y);
                }
                /*if (ra == yri_esp) {
                    YX(0x8D) YX(0x64) YX(0x24) YX(0x04) // leal 4(%esp), %esp

                    YX(0x8B) YX(y86_x_regbyte_8(ra, yri_esp)) // movl offset-4(%esp), $ra
                    YX(0x24) // Extra byte for %esp
                    YXA(&(y->mem[0]) - 4)
                } else {
                }*/
            } else {
                y86_gen_return(y, ys_ins);
            }
            break;
        case yi_bad:
            y86_gen_return(y, ys_ins);
            break;
        default:
            // Impossible
            fprintf(stderr, "Internal bug!\n");
            longjmp(y->jmp, ys_ccf);
            break;
    }
}

void y86_parse(Y_data *y, Y_char **inst, Y_char *end) {
    Y_inst op = **inst & 0xFF;
    (*inst)++;

    Y_reg_id ra = yr_nil;
    Y_reg_id rb = yr_nil;
    Y_word val = 0;

    switch (op) {
        case yi_halt:
        case yi_nop:
        case yi_ret:
            break;

        case yi_rrmovl:
        case yi_cmovle:
        case yi_cmovl:
        case yi_cmove:
        case yi_cmovne:
        case yi_cmovge:
        case yi_cmovg:
        case yi_addl:
        case yi_subl:
        case yi_andl:
        case yi_xorl:
        case yi_pushl:
        case yi_popl:
            // Read registers
            if (*inst == end) op = yi_bad;
            ra = HIGH(**inst);
            rb = LOW(**inst);
            (*inst)++;

            break;

        case yi_irmovl:
        case yi_rmmovl:
        case yi_mrmovl:
            // Read registers
            if (*inst == end) op = yi_bad;
            ra = HIGH(**inst);
            rb = LOW(**inst);
            (*inst)++;

            // Read value
            if (*inst + sizeof(Y_word) > end) op = yi_bad;
            val = IO_WORD(*inst);
            *inst += sizeof(Y_word);

            break;

        case yi_jmp:
        case yi_jle:
        case yi_jl:
        case yi_je:
        case yi_jne:
        case yi_jge:
        case yi_jg:
        case yi_call:
            // Read value
            if (*inst + sizeof(Y_word) > end) op = yi_bad;
            val = IO_WORD(*inst);
            *inst += sizeof(Y_word);

            break;

        case yi_bad:
        default:
            op = yi_bad;

            break;
    }

    y86_gen_x(y, op, ra, rb, val);
}

void y86_load_reset(Y_data *y) {
    Y_word index;
    for (index = 0; index < Y_Y_INST_SIZE; ++index) {
        y->x_map[index] = 0;
    }
    y->x_end = &(y->x_inst[0]);
}

void y86_load(Y_data *y, Y_char *begin) {
    Y_char *inst = begin;
    Y_char *end = &(y->mem[y->reg[yr_len]]);
    while (*end) {
        y->reg[yr_len]++;
        end = &(y->mem[y->reg[yr_len]]);
    }

    Y_word pc = y->reg[yr_pc];

    y86_gen_enter(y);

    while (inst != end) {
        while (inst != end) {
            y->reg[yr_pc] = inst - begin;

            if (y->x_map[y->reg[yr_pc]] && y->x_map[y->reg[yr_pc]] != Y_BAD_ADDR) {
                y86_gen_raw_jmp(y, y->x_map[y->reg[yr_pc]]);
            } else {
                y86_link_x_map(y, y->reg[yr_pc]);
                y86_parse(y, &inst, end);
            }
        }

        for (inst = begin; inst != end; ++inst) {
            if (y->x_map[inst - begin] == Y_BAD_ADDR) break;
        }

        y86_link_x_map(y, y->reg[yr_pc] + 1);
        y86_gen_return(y, ys_hlt);
    };

    y->reg[yr_pc] = pc;
}

void y86_load_all(Y_data *y) {
    y86_load_reset(y);
    y86_load(y, &(y->mem[0]));
}

void y86_load_file_bin(Y_data *y, FILE *binfile) {
    clearerr(binfile);

    y->reg[yr_len] = fread(&(y->mem[0]), sizeof(Y_char), Y_Y_INST_SIZE, binfile);
    if (ferror(binfile)) {
        fprintf(stderr, "fread() failed (0x%x)\n", y->reg[yr_len]);
        longjmp(y->jmp, ys_clf);
    }
    if (!feof(binfile)) {
        fprintf(stderr, "Too large memory footprint (0x%x)\n", y->reg[yr_len]);
        longjmp(y->jmp, ys_clf);
    }
}

void y86_load_file(Y_data *y, Y_char *fname) {
    FILE *binfile = fopen(fname, "rb");

    if (binfile) {
        y86_load_file_bin(y, binfile);
        fclose(binfile);
    } else {
        fprintf(stderr, "Can't open binary file '%s'\n", fname);
        longjmp(y->jmp, ys_clf);
    }
}

void y86_ready(Y_data *y) {
    memcpy(&(y->bak_mem[0]), &(y->mem[0]), sizeof(y->bak_mem));
    memcpy(&(y->bak_reg[0]), &(y->reg[0]), sizeof(y->bak_reg));

    y->reg[yr_cc] = 0x40;
    y->reg[yr_st] = ys_aok;
}

void y86_trace_ip(Y_data *y) {
    y->reg[yr_rey] = (Y_word) &(y->x_inst[0]);
}

void __attribute__ ((noinline)) y86_exec(Y_data *y) {
    __asm__ __volatile__(
        "pushal" "\n\t"
        "pushf" "\n\t"

        "movd %%esp, %%mm0" "\n\t"
        "movl %0, %%esp" "\n\t"

        // Load data
        "movd 12(%%esp), %%mm1" "\n\t"
        "popal" "\n\t"

        // Build callback stack
        "addl $12, %%esp" "\n\t"
        "pushl $y86_fin" "\n\t"
        "movd %%esp, %%mm2" "\n\t"

        "subl $8, %%esp" "\n\t"
        "popf" "\n\t"
        "ret" "\n\t"

    // Finished
    "y86_fin:" "\n\t"

        "pushf" "\n\t"

        // Restore data
        "movd %%mm7, 28(%%esp)" "\n\t"
        "pushal" "\n\t"
        "movd %%mm1, 12(%%esp)" "\n\t"

        "movd %%mm0, %%esp" "\n\t"

        "popf" "\n\t"
        "popal"// "\n\t"
        :
        : "r" (&y->reg[0])
    );
}

void y86_trace_pc(Y_data *y) {
    Y_word index;

    for (index = 0; index < Y_Y_INST_SIZE; ++index) {
        if (y->reg[yr_rey] == (Y_word) y->x_map[index]) {
            y->reg[yr_pc] = index;
            return;
        }
    }
}

Y_word y86_trace_pc_2(Y_data *y, Y_word value) {
    Y_word index;

    if (value >= (Y_word) &y->x_inst[0] && value < (Y_word) &y->x_inst[Y_X_INST_SIZE]) {
        for (index = 0; index < Y_Y_INST_SIZE; ++index) {
            if (value == (Y_word) y->x_map[index]) {
                return index;
            }
        }
    }

    return value;
}

Y_word y86_get_im_ptr() {
    Y_word result;
    __asm__ __volatile__("movd %%mm4, %0": "=r" (result));
    return result;
}

void y86_go(Y_data *y) {
    y86_ready(y);
    y86_trace_ip(y);
    y86_exec(y);
    y86_trace_pc(y);
}

void y86_output_error(Y_data *y) {
    switch (y->reg[yr_st]) {
        case ys_ins:
            fprintf(stdout, "PC = 0x%x, Invalid instruction %.2x\n", y->reg[yr_pc] - 1, y->mem[y->reg[yr_pc] - 1]);
            break;
        case ys_clf:
            fprintf(stdout, "PC = 0x%x, File loading failed\n", /*y->reg[yr_pc] - 1*/ 0);
            break;
        case ys_ccf:
            fprintf(stdout, "PC = 0x%x, Parsing or compiling failed\n", y->reg[yr_pc] - 1);
            break;
        case ys_adp:
            fprintf(stdout, "PC = 0x%x, Invalid instruction address\n", y->reg[yr_pc] - 1);
            break;
        default:
            break;
    }
}

Y_word y86_cc_transform(Y_word cc_x) {
    return ((cc_x >> 11) & 1) | ((cc_x >> 6) & 2) | ((cc_x >> 4) & 4);
}

void y86_output_state(Y_data *y) {
    const Y_char *stat_names[8] = {
        "AOK", "HLT", "ADR", "INS", "", "", "ADR", "INS"
    };

    const Y_char *cc_names[8] = {
        "Z=0 S=0 O=0",
        "Z=0 S=0 O=1",
        "Z=0 S=1 O=0",
        "Z=0 S=1 O=1",
        "Z=1 S=0 O=0",
        "Z=1 S=0 O=1",
        "Z=1 S=1 O=0",
        "Z=1 S=1 O=1"
    };

    fprintf(
        stdout,
        "Stopped at PC = 0x%x.  Status '%s', CC %s\n",
        y->reg[yr_pc] - !!y->reg[yr_st], stat_names[7 & y->reg[yr_st]], cc_names[y86_cc_transform(y->reg[yr_cc])]
    );
}

void y86_output_reg(Y_data *y) {
    Y_reg_lyt index;
    Y_word value1;
    Y_word value2;

    const Y_char *reg_names[yr_cnt] = {
        "%edi", "%esi", "%ebp", "%esp", "%ebx", "%edx", "%ecx", "%eax"
    };

    fprintf(stdout, "Changes to registers:\n");
    for (index = yr_cnt - 1; (Y_word) index >= 0; --index) {
        value1 = y86_trace_pc_2(y, y->bak_reg[index]);
        value2 = y86_trace_pc_2(y, y->reg[index]);

        if (value1 != value2) {
            fprintf(stdout, "%s:\t0x%.8x\t0x%.8x\n", reg_names[index], value1, value2);
        }
    }
}

void y86_output_mem(Y_data *y) {
    Y_word index;
    Y_word value1;
    Y_word value2;

    fprintf(stdout, "Changes to memory:\n");
    for (index = 0; index < Y_MEM_SIZE; index += 4) { // Y_MEM_SIZE = 4 * n
        value1 = y86_trace_pc_2(y, IO_WORD(&(y->bak_mem[index])));
        value2 = y86_trace_pc_2(y, IO_WORD(&(y->mem[index])));

        if (value1 != value2) {
            fprintf(stdout, "0x%.4x:\t0x%.8x\t0x%.8x\n", index, value1, value2);
        }
    }
}

void y86_output(Y_data *y) {
    y86_output_error(y);
    y86_output_state(y);
    y86_output_reg(y);
    fprintf(stdout, "\n");
    y86_output_mem(y);
}

void y86_free(Y_data *y) {
    munmap(y, sizeof(Y_data));
}

void f_usage(Y_char *pname) {
    fprintf(stderr, "Usage: %s file.bin\n", pname);
}

Y_stat f_main(Y_char *fname) {
    Y_data *y = y86_new();
    Y_stat result;
    y->reg[yr_st] = setjmp(y->jmp);

    if (!(y->reg[yr_st])) {
        // Load
        if (strcmp(fname, "nil")) {
            y86_load_file(y, fname);
        } else {
            y->reg[yr_len] = 1;
            y->mem[0] = yi_halt;
        }

        y86_load_all(y);

        // Exec
        y86_go(y);
    } else {
        // Jumped out
    }

    // Output
    y86_output(y);

    // Return
    result = y->reg[yr_st];
    y86_free(y);
    return result == ys_clf || result == ys_ccf;
}

int main(int argc, char *argv[]) {
    switch (argc) {
        // Correct arg
        case 2:
        case 3: // For compatibility
            return f_main(argv[1]);

        // Bad arg or no arg
        default:
            f_usage(argv[0]);
            return 0;
    }
}
