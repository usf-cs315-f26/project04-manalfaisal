#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rv_emu.h"
#include "bits.h"

#define DEBUG 0

static void unsupported(char *s, uint32_t n) {
    printf("unsupported %s 0x%x\n", s, n);
    exit(-1);
}

void emu_r_type(rv_state *rsp, uint32_t iw) {
    uint32_t rd = (iw >> 7) & 0b11111;
    uint32_t rs1 = (iw >> 15) & 0b11111;
    uint32_t rs2 = (iw >> 20) & 0b11111;
    uint32_t funct3 = (iw >> 12) & 0b111;
    uint32_t funct7 = (iw >> 25) & 0b1111111;

	rsp->analysis.ir_count += 1;
	
	switch (funct3) {
    	case 0b000:
    		// add, mul and sub share funct3 000, so funct7 picks which one
    		switch (funct7) {
    			case 0b0000000:
    				// add
    				rsp->regs[rd] = rsp->regs[rs1] + rsp->regs[rs2];
    				break;
    			case 0b0000001:
    				// mul
    				rsp->regs[rd] = rsp->regs[rs1] * rsp->regs[rs2];
    				break;
    			case 0b0100000:
    				// sub
    				rsp->regs[rd] = rsp->regs[rs1] - rsp->regs[rs2];
    				break;
    			default:
    				unsupported("R-type funct7", funct7);
    		}
    		break;
    	case 0b001:
    		// shift left
    		rsp->regs[rd] = rsp->regs[rs1] << rsp->regs[rs2];
    		break;
    	case 0b101:
    		// shift right
    		rsp->regs[rd] = rsp->regs[rs1] >> rsp->regs[rs2];
    		break;
    	case 0b111:
    		// and
    		rsp->regs[rd] = rsp->regs[rs1] & rsp->regs[rs2];
    		break;
    	case 0b100:
    		// div
    		rsp->regs[rd] = (int64_t) rsp->regs[rs1] / (int64_t) rsp->regs[rs2];
    		break;
    	case 0b110:
    		// rem
    		rsp->regs[rd] = (int64_t) rsp->regs[rs1] % (int64_t) rsp->regs[rs2];
			break;
		default:
			unsupported("R-type funct3", funct3);
		}
    rsp->pc += 4; // Next instruction
}

void emu_jalr(rv_state *rsp, uint32_t iw) {
    uint32_t rs1 = (iw >> 15) & 0b11111;  // mask needs five 1s bc 32 registers -> rs1 is 5 bits
    uint64_t val = rsp->regs[rs1];  // Value of regs[1]
	rsp->analysis.j_count += 1;
    rsp->pc = val;  // PC = return address
}
void emu_i_type(rv_state *rsp, uint32_t iw){
	uint32_t rd = (iw >> 7) & 0b11111;
	uint32_t rs1 = (iw >> 15) & 0b11111;
	uint32_t funct3 = (iw >> 12) & 0b111;
	uint32_t shamt = (iw >> 20) & 0b111111;

	uint64_t imm_u = (iw >> 20) & 0b111111111111;
	int64_t imm = ((int64_t) (imm_u << 52)) >> 52;

	rsp->analysis.ir_count += 1;
	
	switch (funct3) {
		case 0b000:
			// addi
			rsp->regs[rd] = rsp->regs[rs1] + imm;
			break;
		case 0b101:
			// srli
			rsp->regs[rd] = rsp->regs[rs1] >> shamt;
			break;
		case 0b001:
			// slli
			rsp->regs[rd] = rsp->regs[rs1] << shamt;
			break;
		default:
			unsupported("I-type funct3", funct3);
	}
	rsp->pc += 4;
}
void emu_b_type(rv_state *rsp, uint32_t iw){
	uint32_t rs1 = (iw >> 15) & 0b11111;
	uint32_t rs2 = (iw >> 20) & 0b11111;
	uint32_t funct3 = (iw >> 12) & 0b111;

	// Extract each piece of the immediate
	uint64_t imm12 = (iw >> 31) & 0b1;
	uint64_t imm10_5 = (iw >> 25) & 0b111111;
	uint64_t imm4_1 = (iw >> 8) & 0b1111;
	uint64_t imm11 = (iw >> 7) & 0b1;

	// OR the pieces back into their real positions
	uint64_t imm_u = (imm12 << 12) | (imm11 << 11) | (imm10_5 << 5) | (imm4_1 << 1);

	// Sign extend: shift up to the top of the register, then back down
	int64_t imm = ((int64_t) (imm_u << 51)) >> 51;

	// Sign compare so cast the register values
	int64_t v1 = (int64_t) rsp->regs[rs1];
	int64_t v2 = (int64_t) rsp->regs[rs2];

	// each case only decides if the branch is taken;
	// updating pc and the counters is the same for every branch
	int taken = 0;

	switch (funct3) {
		case 0b100:
			// blt
			taken = (v1 < v2);
			break;
		case 0b001:
			// bne
			taken = (v1 != v2);
			break;
		case 0b000:
			// beq
			taken = (v1 == v2);
			break;
		case 0b101:
			// bge
			taken = (v1 >= v2);
			break;
		default:
			unsupported("B-type funct3", funct3);
	}

	if (taken) {
		rsp->analysis.b_taken += 1;
		rsp->pc += imm;
	} else {
		rsp->analysis.b_not_taken += 1;
		rsp->pc += 4;
	}
}
void emu_jal(rv_state *rsp, uint32_t iw){
	uint32_t rd = (iw >> 7) & 0b11111;

	// Extract each piece of the immediate
	uint64_t imm20 = (iw >> 31) & 0b1;
	uint64_t imm10_1 = (iw >> 21) & 0b1111111111;
	uint64_t imm11 = (iw >> 20) & 0b1;
	uint64_t imm19_12 = (iw >> 12) & 0b11111111;

	// OR pieces back into real positions
	uint64_t imm_u = (imm20 << 20) | (imm19_12 << 12) | (imm11 << 11) | (imm10_1 << 1);

	// Sign extend a 21 bit immediate
	int64_t imm = ((int64_t) (imm_u << 43)) >> 43;

	rsp->analysis.j_count += 1;
	
	// Link: save return address, unless rd is x0 (plain j)
	if(rd != 0){
		rsp->regs[rd] = rsp->pc + 4;
	}

	rsp->pc += imm;
	
}
void emu_load(rv_state *rsp, uint32_t iw){
	// decode: rd, rs1, funct3
	uint32_t rd = (iw >> 7) & 0b11111;
	uint32_t rs1 = (iw >> 15) & 0b11111;
	uint32_t funct3 = (iw >> 12) & 0b111;
	// decode the 12-bit immediate and sign extend it
	// load offset is a 12 bit I-type immediate 
	// I mask 12 bits and sign-extend with 52 
	uint64_t imm_u = (iw >> 20) & 0b111111111111;
	int64_t imm = ((int64_t) (imm_u << 52)) >> 52;
	// target = base pointer + offset
	uint64_t target = rsp->regs[rs1] + imm;

	rsp->analysis.ld_count += 1;
	
	// read the right size from memory, based on funct3 
	switch (funct3) {
		case 0b000:
			// lb
			rsp->regs[rd] = *(uint8_t *) target;
			break;
		case 0b010:
			// lw
			rsp->regs[rd] = *(uint32_t *) target;
			break;
		case 0b011:
			// ld
			rsp->regs[rd] = *(uint64_t *) target;
			break;
		default:
			unsupported("Load funct3", funct3);
	}
	rsp->pc += 4; 
}
void emu_store(rv_state *rsp, uint32_t iw){
	// decode: rs1, rs2, funct3
	uint32_t rs1 = (iw >> 15) & 0b11111;
	uint32_t rs2 = (iw >> 20) & 0b11111;
	uint32_t funct3 = (iw >> 12) & 0b111;
	// decode the two immediate pieces, put them together, sign extend
	uint64_t imm11_5 = (iw >> 25) & 0b1111111;
	uint64_t imm4_0 = (iw >> 7) & 0b11111;
	uint64_t imm_u = (imm11_5 << 5) | imm4_0;
	int64_t imm = ((int64_t) (imm_u << 52)) >> 52;
	// target = base pointer + offset 
	uint64_t target = rsp->regs[rs1] + imm;

	rsp->analysis.st_count += 1;
	
	// write the right size to memory, based on funct3
	switch (funct3) {
		case 0b000:
			// sb
			*(uint8_t *) target = (uint8_t) rsp->regs[rs2];
			break;
		case 0b010:
			// sw
			*(uint32_t *) target = (uint32_t) rsp->regs[rs2];
			break;
		case 0b011:
			// sd
			*(uint64_t *) target = (uint64_t) rsp->regs[rs2];
			break;
		default:
			unsupported("Store funct3", funct3);
	}
	rsp->pc += 4;
}
static void rv_one(rv_state *state) {
    uint32_t iw  = *((uint32_t*) state->pc);
    iw = cache_lookup(&state->i_cache, (uint64_t) state->pc);

    uint32_t opcode = get_bits(iw, 0, 7);


#if DEBUG
    printf("iw: %x\n", iw);
#endif

	// count every instruction executed
	state->analysis.i_count += 1;
	
    switch (opcode) {
    	case FMT_R:
    		emu_r_type(state, iw);
    		break;
    	case FMT_I_JALR:
    		emu_jalr(state, iw);
    		break;
    	case FMT_I_ARITH:
    		emu_i_type(state, iw);
    		break;
    	case FMT_SB:
    		emu_b_type(state, iw);
    		break;
    	case FMT_UJ:
    		emu_jal(state, iw);
    		break;
    	case FMT_I_LOAD:
    		emu_load(state, iw);
    		break;
    	case FMT_S:
    		emu_store(state, iw);
    		break;
        default:
            unsupported("Unknown opcode: ", opcode);
    }
}

void rv_init(rv_state *state, uint32_t *target, 
             uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    state->pc = (uint64_t) target;
    state->regs[RV_A0] = a0;
    state->regs[RV_A1] = a1;
    state->regs[RV_A2] = a2;
    state->regs[RV_A3] = a3;

    state->regs[RV_ZERO] = 0;  // zero is always 0  (:
    state->regs[RV_RA] = RV_STOP;
    state->regs[RV_SP] = (uint64_t) &state->stack[STACK_SIZE];

    memset(&state->analysis, 0, sizeof(rv_analysis));
    cache_init(&state->i_cache);
}

uint64_t rv_emulate(rv_state *state) {
    while (state->pc != RV_STOP) {
        rv_one(state);
    }
    return state->regs[RV_A0];
}

static void print_pct(char *fmt, int numer, int denom) {
    double pct = 0.0;

    if (denom)
        pct = (double) numer / (double) denom * 100.0;
    printf(fmt, numer, pct);
}

void rv_print(rv_analysis *a) {
    int b_total = a->b_taken + a->b_not_taken;

    printf("=== Analysis\n");
    print_pct("Instructions Executed  = %d\n", a->i_count, a->i_count);
    print_pct("R-type + I-type        = %d (%.2f%%)\n", a->ir_count, a->i_count);
    print_pct("Loads                  = %d (%.2f%%)\n", a->ld_count, a->i_count);
    print_pct("Stores                 = %d (%.2f%%)\n", a->st_count, a->i_count);    
    print_pct("Jumps/JAL/JALR         = %d (%.2f%%)\n", a->j_count, a->i_count);
    print_pct("Conditional branches   = %d (%.2f%%)\n", b_total, a->i_count);
    print_pct("  Branches taken       = %d (%.2f%%)\n", a->b_taken, b_total);
    print_pct("  Branches not taken   = %d (%.2f%%)\n", a->b_not_taken, b_total);
}
