#include <core/kernel/rvemu/cpu.h>

#define OPCODE_LOAD      0x03
#define OPCODE_MISC_MEM  0x0F
#define OPCODE_ALU_IMM   0x13
#define OPCODE_AUIPC     0x17
#define OPCODE_ALU_IMM_W 0x1B
#define OPCODE_STORE     0x23
#define OPCODE_ALU_REG   0x33
#define OPCODE_LUI       0x37
#define OPCODE_ALU_REG_W 0x3B
#define OPCODE_BRANCH    0x63
#define OPCODE_JALR      0x67
#define OPCODE_JAL       0x6F
#define OPCODE_SYSTEM    0x73

static uint64_t sign_extend(uint64_t value, int width_bits) {
    uint64_t sign_bit = (uint64_t)1 << (width_bits - 1);
    if ((value & sign_bit) != 0) {
        return value | (~(sign_bit - 1));
    }
    return value;
}

static uint64_t sign_extend_word(uint64_t value) {
    return (uint64_t)(int64_t)(int32_t)(uint32_t)value;
}

static const char *register_name(int index) {
    static const char *names[CPU_GENERAL_REGISTER_COUNT] = {
        "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
        "s0", "s1", "a0", "a1", "a2", "a3", "a4", "a5",
        "a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7",
        "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"
    };
    if (index >= 0 && index < CPU_GENERAL_REGISTER_COUNT) {
        return names[index];
    }
    return "??";
}

typedef struct trace_builder {
    char *data;
    int length;
    int capacity;
} trace_builder_t;

static void trace_append_char(trace_builder_t *builder, char character) {
    if (builder->length + 1 >= builder->capacity) {
        return;
    }

    builder->data[builder->length] = character;
    builder->length = builder->length + 1;
    builder->data[builder->length] = '\0';
}

static void trace_append_text(trace_builder_t *builder, const char *text) {
    while (*text != '\0') {
        trace_append_char(builder, *text);
        text = text + 1;
    }
}

static void trace_append_hex(trace_builder_t *builder, uint64_t value, int digits) {
    static const char hex_digits[] = "0123456789abcdef";
    for (int index = digits - 1; index >= 0; index--) {
        trace_append_char(builder, hex_digits[(value >> (4 * index)) & 0xF]);
    }
}

static void trace_append_signed(trace_builder_t *builder, int64_t value) {
    if (value < 0) {
        trace_append_char(builder, '-');
        value = -value;
    }
    char digits[20];
    int count = 0;
    while (count == 0 || value != 0) {
        digits[count] = (char)('0' + (value % 10));
        value = value / 10;
        count = count + 1;
    }
    while (count > 0) {
        count = count - 1;
        trace_append_char(builder, digits[count]);
    }
}

static void trace_disassemble(rv64_cpu_t *cpu, rv64_memory_t *memory, rv64_host_t *host, uint32_t instruction, uint64_t pc) {
    char line[256];
    trace_builder_t builder;
    builder.data = line;
    builder.length = 0;
    builder.capacity = 256;

    trace_append_hex(&builder, pc, 16);
    trace_append_text(&builder, ": ");
    trace_append_hex(&builder, (uint64_t)instruction, 8);
    trace_append_text(&builder, "  ");

    uint32_t opcode = instruction & 0x7F;
    uint32_t rd_index = (instruction >> 7) & 0x1F;
    uint32_t funct3 = (instruction >> 12) & 0x07;
    uint32_t rs1_index = (instruction >> 15) & 0x1F;
    uint32_t rs2_index = (instruction >> 20) & 0x1F;
    uint32_t funct7 = (instruction >> 25) & 0x7F;

    switch (opcode) {
        case OPCODE_ALU_IMM: {
            if (funct3 == 0x00) { trace_append_text(&builder, "addi"); }
            else if (funct3 == 0x01) { trace_append_text(&builder, "slli"); }
            else if (funct3 == 0x02) { trace_append_text(&builder, "slti"); }
            else if (funct3 == 0x03) { trace_append_text(&builder, "sltiu"); }
            else if (funct3 == 0x04) { trace_append_text(&builder, "xori"); }
            else if (funct3 == 0x05) {
                if ((instruction & (1U << 30)) != 0) { trace_append_text(&builder, "srai"); }
                else { trace_append_text(&builder, "srli"); }
            }
            else if (funct3 == 0x06) { trace_append_text(&builder, "ori"); }
            else { trace_append_text(&builder, "andi"); }
            trace_append_text(&builder, " ");
            trace_append_text(&builder, register_name((int)rd_index));
            trace_append_text(&builder, ", ");
            trace_append_text(&builder, register_name((int)rs1_index));
            trace_append_text(&builder, ", ");
            if (funct3 == 0x01 || funct3 == 0x05) {
                trace_append_signed(&builder, (int64_t)((instruction >> 20) & 0x3F));
            } else {
                trace_append_signed(&builder, (int64_t)sign_extend((uint64_t)(instruction >> 20), 12));
            }
            break;
        }
        case OPCODE_ALU_IMM_W: {
            if (funct3 == 0x00) { trace_append_text(&builder, "addiw"); }
            else if (funct3 == 0x01) { trace_append_text(&builder, "slliw"); }
            else if (funct3 == 0x05) {
                if ((instruction & (1U << 30)) != 0) { trace_append_text(&builder, "sraiw"); }
                else { trace_append_text(&builder, "srliw"); }
            } else { trace_append_text(&builder, "op-imm-w"); }
            trace_append_text(&builder, " ");
            trace_append_text(&builder, register_name((int)rd_index));
            trace_append_text(&builder, ", ");
            trace_append_text(&builder, register_name((int)rs1_index));
            trace_append_text(&builder, ", ");
            if (funct3 == 0x01 || funct3 == 0x05) {
                trace_append_signed(&builder, (int64_t)((instruction >> 20) & 0x1F));
            } else {
                trace_append_signed(&builder, (int64_t)sign_extend((uint64_t)(instruction >> 20), 12));
            }
            break;
        }
        case OPCODE_ALU_REG:
        case OPCODE_ALU_REG_W: {
            const char *name = "op-reg";
            if (funct3 == 0x00) {
                name = (funct7 == 0x20) ? "sub" : "add";
            } else if (funct3 == 0x01) { name = "sll"; }
            else if (funct3 == 0x02) { name = "slt"; }
            else if (funct3 == 0x03) { name = "sltu"; }
            else if (funct3 == 0x04) { name = "xor"; }
            else if (funct3 == 0x05) { name = (funct7 == 0x20) ? "sra" : "srl"; }
            else if (funct3 == 0x06) { name = "or"; }
            else if (funct3 == 0x07) { name = "and"; }
            if (opcode == OPCODE_ALU_REG_W && funct3 != 0x05) {
                trace_append_text(&builder, name);
                trace_append_text(&builder, "w");
            } else {
                trace_append_text(&builder, name);
            }
            trace_append_text(&builder, " ");
            trace_append_text(&builder, register_name((int)rd_index));
            trace_append_text(&builder, ", ");
            trace_append_text(&builder, register_name((int)rs1_index));
            trace_append_text(&builder, ", ");
            trace_append_text(&builder, register_name((int)rs2_index));
            break;
        }
        case OPCODE_LOAD: {
            static const char *names[8] = { "lb", "lh", "lw", "ld", "lbu", "lhu", "lwu", "load?" };
            trace_append_text(&builder, names[funct3]);
            trace_append_text(&builder, " ");
            trace_append_text(&builder, register_name((int)rd_index));
            trace_append_text(&builder, ", ");
            trace_append_signed(&builder, (int64_t)sign_extend((uint64_t)(instruction >> 20), 12));
            trace_append_text(&builder, "(");
            trace_append_text(&builder, register_name((int)rs1_index));
            trace_append_text(&builder, ")");
            break;
        }
        case OPCODE_STORE: {
            static const char *names[8] = { "sb", "sh", "sw", "sd", "store?", "store?", "store?", "store?" };
            trace_append_text(&builder, names[funct3]);
            trace_append_text(&builder, " ");
            trace_append_text(&builder, register_name((int)rs2_index));
            trace_append_text(&builder, ", ");
            trace_append_signed(&builder, (int64_t)sign_extend(
                (uint64_t)((instruction >> 7) & 0x1F) | ((uint64_t)(instruction >> 25) << 5), 12));
            trace_append_text(&builder, "(");
            trace_append_text(&builder, register_name((int)rs1_index));
            trace_append_text(&builder, ")");
            break;
        }
        case OPCODE_BRANCH: {
            static const char *names[8] = { "beq", "bne", "branch?", "branch?", "blt", "bge", "bltu", "bgeu" };
            uint64_t immediate = sign_extend(
                (uint64_t)((instruction >> 31) & 1) << 12
                | (uint64_t)((instruction >> 7) & 1) << 11
                | (uint64_t)((instruction >> 25) & 0x3F) << 5
                | (uint64_t)((instruction >> 8) & 0x0F) << 1, 13);
            trace_append_text(&builder, names[funct3]);
            trace_append_text(&builder, " ");
            trace_append_text(&builder, register_name((int)rs1_index));
            trace_append_text(&builder, ", ");
            trace_append_text(&builder, register_name((int)rs2_index));
            trace_append_text(&builder, ", ");
            trace_append_hex(&builder, pc + immediate, 16);
            break;
        }
        case OPCODE_JAL: {
            uint64_t immediate = sign_extend(
                (uint64_t)(instruction >> 31) << 20
                | (uint64_t)((instruction >> 21) & 0x3FF) << 1
                | (uint64_t)((instruction >> 20) & 1) << 11
                | (uint64_t)((instruction >> 12) & 0xFF) << 12, 21);
            trace_append_text(&builder, "jal ");
            trace_append_text(&builder, register_name((int)rd_index));
            trace_append_text(&builder, ", ");
            trace_append_hex(&builder, pc + immediate, 16);
            break;
        }
        case OPCODE_JALR: {
            trace_append_text(&builder, "jalr ");
            trace_append_text(&builder, register_name((int)rd_index));
            trace_append_text(&builder, ", ");
            trace_append_signed(&builder, (int64_t)sign_extend((uint64_t)(instruction >> 20), 12));
            trace_append_text(&builder, "(");
            trace_append_text(&builder, register_name((int)rs1_index));
            trace_append_text(&builder, ")");
            break;
        }
        case OPCODE_LUI: {
            trace_append_text(&builder, "lui ");
            trace_append_text(&builder, register_name((int)rd_index));
            trace_append_text(&builder, ", ");
            trace_append_hex(&builder, (uint64_t)(instruction >> 12) & 0xFFFFF, 5);
            break;
        }
        case OPCODE_AUIPC: {
            trace_append_text(&builder, "auipc ");
            trace_append_text(&builder, register_name((int)rd_index));
            trace_append_text(&builder, ", ");
            trace_append_hex(&builder, (uint64_t)(instruction >> 12) & 0xFFFFF, 5);
            break;
        }
        case OPCODE_MISC_MEM: {
            trace_append_text(&builder, "fence");
            break;
        }
        case OPCODE_SYSTEM: {
            if (instruction == 0x00000073) { trace_append_text(&builder, "ecall"); }
            else if (instruction == 0x00100073) { trace_append_text(&builder, "ebreak"); }
            else { trace_append_text(&builder, "system?"); }
            break;
        }
        default: {
            trace_append_text(&builder, "opcode=");
            trace_append_hex(&builder, (uint64_t)opcode, 2);
            break;
        }
    }

    trace_append_char(&builder, '\n');
    (void)cpu;
    host->write_error(line, (uint64_t)builder.length, host->context);
}

// instructions execution
static void execute_alu_immediate(rv64_cpu_t *cpu, uint32_t instruction, uint64_t pc) {
    (void)pc;
    uint32_t rd_index = (instruction >> 7) & 0x1F;
    uint32_t funct3 = (instruction >> 12) & 0x07;
    uint32_t rs1_index = (instruction >> 15) & 0x1F;
    uint64_t immediate = sign_extend((uint64_t)(instruction >> 20), 12);
    uint64_t left = cpu_read_register(cpu, (int)rs1_index);

    if (funct3 == 0x00) {
        cpu_write_register(cpu, (int)rd_index, left + immediate);
    } else if (funct3 == 0x01) {
        uint32_t shift_amount = (instruction >> 20) & 0x3F;
        cpu_write_register(cpu, (int)rd_index, left << shift_amount);
    } else if (funct3 == 0x02) {
        int result = ((int64_t)left < (int64_t)immediate) ? 1 : 0;
        cpu_write_register(cpu, (int)rd_index, (uint64_t)result);
    } else if (funct3 == 0x03) {
        int result = (left < immediate) ? 1 : 0;
        cpu_write_register(cpu, (int)rd_index, (uint64_t)result);
    } else if (funct3 == 0x04) {
        cpu_write_register(cpu, (int)rd_index, left ^ immediate);
    } else if (funct3 == 0x05) {
        uint32_t shift_amount = (instruction >> 20) & 0x3F;
        if ((instruction & (1U << 30)) != 0) {
            cpu_write_register(cpu, (int)rd_index, (uint64_t)((int64_t)left >> shift_amount));
        } else {
            cpu_write_register(cpu, (int)rd_index, left >> shift_amount);
        }
    } else if (funct3 == 0x06) {
        cpu_write_register(cpu, (int)rd_index, left | immediate);
    } else {
        cpu_write_register(cpu, (int)rd_index, left & immediate);
    }
}

static void execute_alu_immediate_word(rv64_cpu_t *cpu, uint32_t instruction) {
    uint32_t rd_index = (instruction >> 7) & 0x1F;
    uint32_t funct3 = (instruction >> 12) & 0x07;
    uint32_t rs1_index = (instruction >> 15) & 0x1F;
    uint64_t immediate = sign_extend((uint64_t)(instruction >> 20), 12);
    uint64_t left = cpu_read_register(cpu, (int)rs1_index);

    if (funct3 == 0x00) {
        uint64_t result = sign_extend_word((uint32_t)left + (uint32_t)immediate);
        cpu_write_register(cpu, (int)rd_index, result);
    } else if (funct3 == 0x01) {
        uint32_t shift_amount = (instruction >> 20) & 0x1F;
        uint64_t result = sign_extend_word((uint32_t)((uint32_t)left << shift_amount));
        cpu_write_register(cpu, (int)rd_index, result);
    } else if (funct3 == 0x05) {
        uint32_t shift_amount = (instruction >> 20) & 0x1F;
        if ((instruction & (1U << 30)) != 0) {
            uint64_t result = sign_extend_word((uint32_t)((int32_t)left >> shift_amount));
            cpu_write_register(cpu, (int)rd_index, result);
        } else {
            uint64_t result = sign_extend_word((uint32_t)((uint32_t)left >> shift_amount));
            cpu_write_register(cpu, (int)rd_index, result);
        }
    }
}

static void execute_alu_register(rv64_cpu_t *cpu, uint32_t instruction) {
    uint32_t rd_index = (instruction >> 7) & 0x1F;
    uint32_t funct3 = (instruction >> 12) & 0x07;
    uint32_t rs1_index = (instruction >> 15) & 0x1F;
    uint32_t rs2_index = (instruction >> 20) & 0x1F;
    uint32_t funct7 = (instruction >> 25) & 0x7F;
    uint64_t left = cpu_read_register(cpu, (int)rs1_index);
    uint64_t right = cpu_read_register(cpu, (int)rs2_index);

    if (funct3 == 0x00) {
        if (funct7 == 0x20) {
            cpu_write_register(cpu, (int)rd_index, left - right);
        } else {
            cpu_write_register(cpu, (int)rd_index, left + right);
        }
    } else if (funct3 == 0x01) {
        uint32_t shift_amount = (uint32_t)(right & 0x3F);
        cpu_write_register(cpu, (int)rd_index, left << shift_amount);
    } else if (funct3 == 0x02) {
        int result = ((int64_t)left < (int64_t)right) ? 1 : 0;
        cpu_write_register(cpu, (int)rd_index, (uint64_t)result);
    } else if (funct3 == 0x03) {
        int result = (left < right) ? 1 : 0;
        cpu_write_register(cpu, (int)rd_index, (uint64_t)result);
    } else if (funct3 == 0x04) {
        cpu_write_register(cpu, (int)rd_index, left ^ right);
    } else if (funct3 == 0x05) {
        uint32_t shift_amount = (uint32_t)(right & 0x3F);
        if (funct7 == 0x20) {
            cpu_write_register(cpu, (int)rd_index, (uint64_t)((int64_t)left >> shift_amount));
        } else {
            cpu_write_register(cpu, (int)rd_index, left >> shift_amount);
        }
    } else if (funct3 == 0x06) {
        cpu_write_register(cpu, (int)rd_index, left | right);
    } else {
        cpu_write_register(cpu, (int)rd_index, left & right);
    }
}

static void execute_alu_register_word(rv64_cpu_t *cpu, uint32_t instruction) {
    uint32_t rd_index = (instruction >> 7) & 0x1F;
    uint32_t funct3 = (instruction >> 12) & 0x07;
    uint32_t rs1_index = (instruction >> 15) & 0x1F;
    uint32_t rs2_index = (instruction >> 20) & 0x1F;
    uint32_t funct7 = (instruction >> 25) & 0x7F;
    uint64_t left = cpu_read_register(cpu, (int)rs1_index);
    uint64_t right = cpu_read_register(cpu, (int)rs2_index);

    if (funct3 == 0x00) {
        if (funct7 == 0x20) {
            uint64_t result = sign_extend_word((uint32_t)left - (uint32_t)right);
            cpu_write_register(cpu, (int)rd_index, result);
        } else {
            uint64_t result = sign_extend_word((uint32_t)left + (uint32_t)right);
            cpu_write_register(cpu, (int)rd_index, result);
        }
    } else if (funct3 == 0x01) {
        uint32_t shift_amount = (uint32_t)(right & 0x1F);
        uint64_t result = sign_extend_word((uint32_t)((uint32_t)left << shift_amount));
        cpu_write_register(cpu, (int)rd_index, result);
    } else if (funct3 == 0x05) {
        uint32_t shift_amount = (uint32_t)(right & 0x1F);
        if (funct7 == 0x20) {
            uint64_t result = sign_extend_word((uint32_t)((int32_t)left >> shift_amount));
            cpu_write_register(cpu, (int)rd_index, result);
        } else {
            uint64_t result = sign_extend_word((uint32_t)((uint32_t)left >> shift_amount));
            cpu_write_register(cpu, (int)rd_index, result);
        }
    }
}

static void execute_load(rv64_cpu_t *cpu, rv64_memory_t *memory, rv64_host_t *host, uint32_t instruction, uint64_t pc) {
    (void)pc;
    uint32_t rd_index = (instruction >> 7) & 0x1F;
    uint32_t funct3 = (instruction >> 12) & 0x07;
    uint32_t rs1_index = (instruction >> 15) & 0x1F;
    uint64_t immediate = sign_extend((uint64_t)(instruction >> 20), 12);
    uint64_t effective_address = cpu_read_register(cpu, (int)rs1_index) + immediate;

    uint64_t loaded = 0;
    int failed = 0;

    if (funct3 == 0x00) {
        uint8_t raw = 0;
        failed = memory_read_u8(memory, effective_address, &raw);
        loaded = sign_extend((uint64_t)raw, 8);
    } else if (funct3 == 0x01) {
        uint16_t raw = 0;
        failed = memory_read_u16(memory, effective_address, &raw);
        loaded = sign_extend((uint64_t)raw, 16);
    } else if (funct3 == 0x02) {
        uint32_t raw = 0;
        failed = memory_read_u32(memory, effective_address, &raw);
        loaded = sign_extend_word((uint64_t)raw);
    } else if (funct3 == 0x03) {
        failed = memory_read_u64(memory, effective_address, &loaded);
    } else if (funct3 == 0x04) {
        uint8_t raw = 0;
        failed = memory_read_u8(memory, effective_address, &raw);
        loaded = (uint64_t)raw;
    } else if (funct3 == 0x05) {
        uint16_t raw = 0;
        failed = memory_read_u16(memory, effective_address, &raw);
        loaded = (uint64_t)raw;
    } else if (funct3 == 0x06) {
        uint32_t raw = 0;
        failed = memory_read_u32(memory, effective_address, &raw);
        loaded = (uint64_t)raw;
    }

    if (failed != 0) {
        host->report_error("load: out of bounds", effective_address, 0, host->context);
        cpu->halted = 1;
        return;
    }
    cpu_write_register(cpu, (int)rd_index, loaded);
}

static void execute_store(rv64_cpu_t *cpu, rv64_memory_t *memory, rv64_host_t *host, uint32_t instruction, uint64_t pc) {
    uint32_t funct3 = (instruction >> 12) & 0x07;
    uint32_t rs1_index = (instruction >> 15) & 0x1F;
    uint32_t rs2_index = (instruction >> 20) & 0x1F;
    uint64_t immediate = sign_extend((uint64_t)((instruction >> 7) & 0x1F) | ((uint64_t)(instruction >> 25) << 5), 12);
    uint64_t effective_address = cpu_read_register(cpu, (int)rs1_index) + immediate;
    uint64_t value = cpu_read_register(cpu, (int)rs2_index);

    int failed = 0;
    if (funct3 == 0x00) {
        failed = memory_write_u8(memory, effective_address, (uint8_t)value);
    } else if (funct3 == 0x01) {
        failed = memory_write_u16(memory, effective_address, (uint16_t)value);
    } else if (funct3 == 0x02) {
        failed = memory_write_u32(memory, effective_address, (uint32_t)value);
    } else if (funct3 == 0x03) {
        failed = memory_write_u64(memory, effective_address, value);
    }

    if (failed != 0) {
        host->report_error("store: out of bounds", effective_address, 0, host->context);
        cpu->halted = 1;
    }
}

static void execute_branch(rv64_cpu_t *cpu, uint32_t instruction, uint64_t pc) {
    uint32_t funct3 = (instruction >> 12) & 0x07;
    uint32_t rs1_index = (instruction >> 15) & 0x1F;
    uint32_t rs2_index = (instruction >> 20) & 0x1F;
    uint64_t immediate = sign_extend(
        (uint64_t)((instruction >> 31) & 1) << 12
        | (uint64_t)((instruction >> 7) & 1) << 11
        | (uint64_t)((instruction >> 25) & 0x3F) << 5
        | (uint64_t)((instruction >> 8) & 0x0F) << 1, 13);
    uint64_t left = cpu_read_register(cpu, (int)rs1_index);
    uint64_t right = cpu_read_register(cpu, (int)rs2_index);

    int taken = 0;
    if (funct3 == 0x00) {
        taken = (left == right);
    } else if (funct3 == 0x01) {
        taken = (left != right);
    } else if (funct3 == 0x04) {
        taken = ((int64_t)left < (int64_t)right);
    } else if (funct3 == 0x05) {
        taken = ((int64_t)left >= (int64_t)right);
    } else if (funct3 == 0x06) {
        taken = (left < right);
    } else if (funct3 == 0x07) {
        taken = (left >= right);
    }

    if (taken != 0) {
        cpu->pc = pc + immediate;
    }
}

static void execute_jal(rv64_cpu_t *cpu, uint32_t instruction, uint64_t pc) {
    uint32_t rd_index = (instruction >> 7) & 0x1F;
    uint64_t immediate = sign_extend(
        (uint64_t)(instruction >> 31) << 20
        | (uint64_t)((instruction >> 21) & 0x3FF) << 1
        | (uint64_t)((instruction >> 20) & 1) << 11
        | (uint64_t)((instruction >> 12) & 0xFF) << 12, 21);
    uint64_t link_address = cpu->pc;
    cpu->pc = pc + immediate;
    cpu_write_register(cpu, (int)rd_index, link_address);
}

static void execute_jalr(rv64_cpu_t *cpu, rv64_host_t *host, uint32_t instruction) {
    uint32_t rd_index = (instruction >> 7) & 0x1F;
    uint32_t funct3 = (instruction >> 12) & 0x07;
    uint32_t rs1_index = (instruction >> 15) & 0x1F;
    uint64_t immediate = sign_extend((uint64_t)(instruction >> 20), 12);

    if (funct3 != 0x00) {
        host->report_error("jalr: reserved funct3", funct3, 0, host->context);
        cpu->halted = 1;
        return;
    }

    uint64_t target = (cpu_read_register(cpu, (int)rs1_index) + immediate) & ~(uint64_t)1;
    uint64_t link_address = cpu->pc;
    cpu->pc = target;
    cpu_write_register(cpu, (int)rd_index, link_address);
}

static void execute_upper(rv64_cpu_t *cpu, uint32_t opcode, uint32_t instruction, uint64_t pc) {
    uint32_t rd_index = (instruction >> 7) & 0x1F;
    uint64_t immediate = sign_extend_word((uint64_t)(instruction & 0xFFFFF000));

    if (opcode == OPCODE_LUI) {
        cpu_write_register(cpu, (int)rd_index, immediate);
    } else {
        cpu_write_register(cpu, (int)rd_index, pc + immediate);
    }
}

static void execute_ecall(rv64_cpu_t *cpu, rv64_memory_t *memory, rv64_host_t *host) {
    if (cpu->ecall_dispatcher == NULL) {
        host->report_error("ecall: no dispatcher installed", cpu->pc, 0, host->context);
        cpu->halted = 1;
        return;
    }
    cpu->ecall_dispatcher(cpu->ecall_dispatcher_context, cpu, memory, host);
}

static void execute_instruction(rv64_cpu_t *cpu, rv64_memory_t *memory, rv64_host_t *host, uint32_t instruction, uint64_t pc) {
    uint32_t opcode = instruction & 0x7F;

    switch (opcode) {
        case OPCODE_LOAD:
            execute_load(cpu, memory, host, instruction, pc);
            break;
        case OPCODE_STORE:
            execute_store(cpu, memory, host, instruction, pc);
            break;
        case OPCODE_ALU_IMM:
            execute_alu_immediate(cpu, instruction, pc);
            break;
        case OPCODE_ALU_IMM_W:
            execute_alu_immediate_word(cpu, instruction);
            break;
        case OPCODE_ALU_REG:
            execute_alu_register(cpu, instruction);
            break;
        case OPCODE_ALU_REG_W:
            execute_alu_register_word(cpu, instruction);
            break;
        case OPCODE_BRANCH:
            execute_branch(cpu, instruction, pc);
            break;
        case OPCODE_JAL:
            execute_jal(cpu, instruction, pc);
            break;
        case OPCODE_JALR:
            execute_jalr(cpu, host, instruction);
            break;
        case OPCODE_LUI:
        case OPCODE_AUIPC:
            execute_upper(cpu, opcode, instruction, pc);
            break;
        case OPCODE_MISC_MEM:
            break;
        case OPCODE_SYSTEM:
            if (instruction == 0x00000073) {
                execute_ecall(cpu, memory, host);
            } else if (instruction == 0x00100073) {
                host->report_error("ebreak", pc, 0, host->context);
                cpu->halted = 1;
            } else {
                host->report_error("system: unsupported instruction", instruction, pc, host->context);
                cpu->halted = 1;
            }
            break;
        default:
            host->report_error("illegal instruction opcode", opcode, pc, host->context);
            cpu->halted = 1;
            break;
    }
}

// public API

void cpu_reset(rv64_cpu_t *cpu, uint64_t entry_point, uint64_t stack_pointer) {
    for (int index = 0; index < CPU_GENERAL_REGISTER_COUNT; index++) {
        cpu->registers[index] = 0;
    }
    cpu->registers[2] = stack_pointer;
    cpu->pc = entry_point;
    cpu->halted = 0;
    cpu->trace_enabled = 0;
    cpu->ecall_dispatcher = NULL;
    cpu->ecall_dispatcher_context = NULL;
}

void cpu_set_ecall_dispatcher(rv64_cpu_t *cpu, ecall_dispatcher_fn dispatcher, void *dispatcher_context) {
    cpu->ecall_dispatcher = dispatcher;
    cpu->ecall_dispatcher_context = dispatcher_context;
}

uint64_t cpu_read_register(rv64_cpu_t *cpu, int index) {
    if (index == 0) {
        return 0;
    }
    if (index < 0 || index >= CPU_GENERAL_REGISTER_COUNT) {
        return 0;
    }
    return cpu->registers[index];
}

void cpu_write_register(rv64_cpu_t *cpu, int index, uint64_t value) {
    if (index == 0) {
        return;
    }
    if (index < 0 || index >= CPU_GENERAL_REGISTER_COUNT) {
        return;
    }
    cpu->registers[index] = value;
}

int cpu_step(rv64_cpu_t *cpu, rv64_memory_t *memory, rv64_host_t *host) {
    if (cpu->halted) {
        return 1;
    }

    uint64_t current_pc = cpu->pc;
    if ((current_pc & 0x3) != 0) {
        host->report_error("misaligned pc", current_pc, 0, host->context);
        cpu->halted = 1;
        return 1;
    }

    uint32_t instruction = 0;
    if (memory_read_u32(memory, current_pc, &instruction) != 0) {
        host->report_error("instruction fetch: out of bounds", current_pc, 0, host->context);
        cpu->halted = 1;
        return 1;
    }

    cpu->pc = current_pc + 4;

    if (cpu->trace_enabled) {
        trace_disassemble(cpu, memory, host, instruction, current_pc);
    }

    execute_instruction(cpu, memory, host, instruction, current_pc);

    if (cpu->halted) {
        return 1;
    }
    return 0;
}

void cpu_run(rv64_cpu_t *cpu, rv64_memory_t *memory, rv64_host_t *host) {
    while (!cpu->halted) {
        if (cpu_step(cpu, memory, host) != 0) {
            break;
        }
    }
}