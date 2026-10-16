#include "ss_policy.h"

#include <cstring>

#define FPU SSP_FPU
#define PRIV SSP_PRIVILEGED
#define FPR (SSP_FPU | SSP_FP_RESULT)
#define FPRD (SSP_FPU | SSP_FP_RESULT | SSP_DOUBLE)

//Reasons that more than one file shares
static const char known_oix[] =
	"nullDC: the operand cache as ram hack is keyed on address bit 25 alone, not on CCR.OIX and the cache area";
static const char known_sum_of_products[] =
	"nullDC rounds (and overflows) every product and every sum to single precision and adds the products in one "
	"order in both engines, FIPR in pairs, (p0 + p1) + (p2 + p3), FTRV one by one, ((p0 + p1) + p2) + p3. The vectors "
	"carry the fused multiply-add of the host that generated them. Neither is the SH4's sum of products";

//One row per vector file: name, instruction, width of the data read and of the data write, flags, known failures
static const SsFilePolicy policy[] =
{
	{ "0000000000001000_sz0_pr0", "clrt", 0, 0, 0, 0 },
	{ "0000000000001001_sz0_pr0", "nop", 0, 0, 0, 0 },
	{ "0000000000001011_sz0_pr0", "rts", 0, 0, 0, 0 },
	{ "0000000000011000_sz0_pr0", "sett", 0, 0, 0, 0 },
	{ "0000000000011001_sz0_pr0", "div0u", 0, 0, 0, 0 },
	{ "0000000000011011_sz0_pr0", "sleep", 0, 0, PRIV, 0 },
	{ "0000000000101000_sz0_pr0", "clrmac", 0, 0, 0, 0 },
	{ "0000000000101011_sz0_pr0", "rte", 0, 0, PRIV | SSP_RTE, 0 },
	{ "0000000000111000_sz0_pr0", "ldtlb", 0, 0, PRIV, 0 },
	{ "0000000001001000_sz0_pr0", "clrs", 0, 0, 0, 0 },
	{ "0000000001011000_sz0_pr0", "sets", 0, 0, 0, 0 },
	{ "0000mmmm00000011_sz0_pr0", "bsrf rm", 0, 0, 0, 0 },
	{ "0000mmmm00100011_sz0_pr0", "braf rm", 0, 0, 0, 0 },
	{ "0000nnnn00000010_sz0_pr0", "stc sr,rn", 0, 0, PRIV, 0 },
	{ "0000nnnn00001010_sz0_pr0", "sts mach,rn", 0, 0, 0, 0 },
	{ "0000nnnn00010010_sz0_pr0", "stc gbr,rn", 0, 0, 0, 0 },
	{ "0000nnnn00011010_sz0_pr0", "sts macl,rn", 0, 0, 0, 0 },
	{ "0000nnnn00100010_sz0_pr0", "stc vbr,rn", 0, 0, PRIV, 0 },
	{ "0000nnnn00101001_sz0_pr0", "movt rn", 0, 0, 0, 0 },
	{ "0000nnnn00101010_sz0_pr0", "sts pr,rn", 0, 0, 0, 0 },
	{ "0000nnnn00110010_sz0_pr0", "stc ssr,rn", 0, 0, PRIV, 0 },
	{ "0000nnnn00111010_sz0_pr0", "stc sgr,rn", 0, 0, PRIV, 0 },
	{ "0000nnnn01000010_sz0_pr0", "stc spc,rn", 0, 0, PRIV, 0 },
	{ "0000nnnn01011010_sz0_pr0", "sts fpul,rn", 0, 0, FPU, 0 },
	{ "0000nnnn01101010_sz0_pr0", "sts fpscr,rn", 0, 0, FPU, 0 },
	{ "0000nnnn10010011_sz0_pr0", "ocbi @rn", 0, 0, 0, 0 },
	{ "0000nnnn10100011_sz0_pr0", "ocbp @rn", 0, 0, 0, 0 },
	{ "0000nnnn10110011_sz0_pr0", "ocbwb @rn", 0, 0, SSP_MAY_CRASH, known_oix },
	{ "0000nnnn11000011_sz0_pr0", "movca.l r0,@rn", 0, 4, 0, known_oix },
	{ "0000nnnn11111010_sz0_pr0", "stc dbr,rn", 0, 0, PRIV, 0 },
	{ "0000nnnn1mmm0010_sz0_pr0", "stc rm_bank,rn", 0, 0, PRIV, 0 },
	{ "0000nnnnmmmm0100_sz0_pr0", "mov.b rm,@(r0,rn)", 0, 1, 0, 0 },
	{ "0000nnnnmmmm0101_sz0_pr0", "mov.w rm,@(r0,rn)", 0, 2, 0, 0 },
	{ "0000nnnnmmmm0110_sz0_pr0", "mov.l rm,@(r0,rn)", 0, 4, 0, 0 },
	{ "0000nnnnmmmm0111_sz0_pr0", "mul.l rm,rn", 0, 0, 0, 0 },
	{ "0000nnnnmmmm1100_sz0_pr0", "mov.b @(r0,rm),rn", 1, 0, 0, 0 },
	{ "0000nnnnmmmm1101_sz0_pr0", "mov.w @(r0,rm),rn", 2, 0, 0, 0 },
	{ "0000nnnnmmmm1110_sz0_pr0", "mov.l @(r0,rm),rn", 4, 0, 0, 0 },
	{ "0001nnnnmmmmdddd_sz0_pr0", "mov.l rm,@(disp,rn)", 0, 4, 0, 0 },
	{ "0010nnnnmmmm0000_sz0_pr0", "mov.b rm,@rn", 0, 1, 0, 0 },
	{ "0010nnnnmmmm0001_sz0_pr0", "mov.w rm,@rn", 0, 2, 0, 0 },
	{ "0010nnnnmmmm0010_sz0_pr0", "mov.l rm,@rn", 0, 4, 0, 0 },
	{ "0010nnnnmmmm0100_sz0_pr0", "mov.b rm,@-rn", 0, 1, 0, 0 },
	{ "0010nnnnmmmm0101_sz0_pr0", "mov.w rm,@-rn", 0, 2, 0, 0 },
	{ "0010nnnnmmmm0110_sz0_pr0", "mov.l rm,@-rn", 0, 4, 0, 0 },
	{ "0010nnnnmmmm0111_sz0_pr0", "div0s rm,rn", 0, 0, 0, 0 },
	{ "0010nnnnmmmm1000_sz0_pr0", "tst rm,rn", 0, 0, 0, 0 },
	{ "0010nnnnmmmm1001_sz0_pr0", "and rm,rn", 0, 0, 0, 0 },
	{ "0010nnnnmmmm1010_sz0_pr0", "xor rm,rn", 0, 0, 0, 0 },
	{ "0010nnnnmmmm1011_sz0_pr0", "or rm,rn", 0, 0, 0, 0 },
	{ "0010nnnnmmmm1100_sz0_pr0", "cmp/str rm,rn", 0, 0, 0, 0 },
	{ "0010nnnnmmmm1101_sz0_pr0", "xtrct rm,rn", 0, 0, 0, 0 },
	{ "0010nnnnmmmm1110_sz0_pr0", "mulu.w rm,rn", 0, 0, 0, 0 },
	{ "0010nnnnmmmm1111_sz0_pr0", "muls.w rm,rn", 0, 0, 0, 0 },
	{ "0011nnnnmmmm0000_sz0_pr0", "cmp/eq rm,rn", 0, 0, 0, 0 },
	{ "0011nnnnmmmm0010_sz0_pr0", "cmp/hs rm,rn", 0, 0, 0, 0 },
	{ "0011nnnnmmmm0011_sz0_pr0", "cmp/ge rm,rn", 0, 0, 0, 0 },
	{ "0011nnnnmmmm0100_sz0_pr0", "div1 rm,rn", 0, 0, 0, 0 },
	{ "0011nnnnmmmm0101_sz0_pr0", "dmulu.l rm,rn", 0, 0, 0, 0 },
	{ "0011nnnnmmmm0110_sz0_pr0", "cmp/hi rm,rn", 0, 0, 0, 0 },
	{ "0011nnnnmmmm0111_sz0_pr0", "cmp/gt rm,rn", 0, 0, 0, 0 },
	{ "0011nnnnmmmm1000_sz0_pr0", "sub rm,rn", 0, 0, 0, 0 },
	{ "0011nnnnmmmm1010_sz0_pr0", "subc rm,rn", 0, 0, 0, 0 },
	{ "0011nnnnmmmm1011_sz0_pr0", "subv rm,rn", 0, 0, 0, 0 },
	{ "0011nnnnmmmm1100_sz0_pr0", "add rm,rn", 0, 0, 0, 0 },
	{ "0011nnnnmmmm1101_sz0_pr0", "dmuls.l rm,rn", 0, 0, 0, 0 },
	{ "0011nnnnmmmm1110_sz0_pr0", "addc rm,rn", 0, 0, 0, 0 },
	{ "0011nnnnmmmm1111_sz0_pr0", "addv rm,rn", 0, 0, 0, 0 },
	{ "0100mmmm00000110_sz0_pr0", "lds.l @rm+,mach", 4, 0, 0, 0 },
	{ "0100mmmm00000111_sz0_pr0", "ldc.l @rm+,sr", 4, 0, PRIV, 0 },
	{ "0100mmmm00001010_sz0_pr0", "lds rm,mach", 0, 0, 0, 0 },
	{ "0100mmmm00001011_sz0_pr0", "jsr @rm", 0, 0, 0, 0 },
	{ "0100mmmm00001110_sz0_pr0", "ldc rm,sr", 0, 0, PRIV, 0 },
	{ "0100mmmm00010110_sz0_pr0", "lds.l @rm+,macl", 4, 0, 0, 0 },
	{ "0100mmmm00010111_sz0_pr0", "ldc.l @rm+,gbr", 4, 0, 0, 0 },
	{ "0100mmmm00011010_sz0_pr0", "lds rm,macl", 0, 0, 0, 0 },
	{ "0100mmmm00011110_sz0_pr0", "ldc rm,gbr", 0, 0, 0, 0 },
	{ "0100mmmm00100110_sz0_pr0", "lds.l @rm+,pr", 4, 0, 0, 0 },
	{ "0100mmmm00100111_sz0_pr0", "ldc.l @rm+,vbr", 4, 0, PRIV, 0 },
	{ "0100mmmm00101010_sz0_pr0", "lds rm,pr", 0, 0, 0, 0 },
	{ "0100mmmm00101011_sz0_pr0", "jmp @rm", 0, 0, 0, 0 },
	{ "0100mmmm00101110_sz0_pr0", "ldc rm,vbr", 0, 0, PRIV, 0 },
	{ "0100mmmm00110111_sz0_pr0", "ldc.l @rm+,ssr", 4, 0, PRIV, 0 },
	{ "0100mmmm00111110_sz0_pr0", "ldc rm,ssr", 0, 0, PRIV, 0 },
	{ "0100mmmm01000111_sz0_pr0", "ldc.l @rm+,spc", 4, 0, PRIV, 0 },
	{ "0100mmmm01001110_sz0_pr0", "ldc rm,spc", 0, 0, PRIV, 0 },
	{ "0100mmmm01010110_sz0_pr0", "lds.l @rm+,fpul", 4, 0, FPU, 0 },
	{ "0100mmmm01011010_sz0_pr0", "lds rm,fpul", 0, 0, FPU, 0 },
	{ "0100mmmm01100110_sz0_pr0", "lds.l @rm+,fpscr", 4, 0, FPU, 0 },
	{ "0100mmmm01101010_sz0_pr0", "lds rm,fpscr", 0, 0, FPU, 0 },
	{ "0100mmmm11110110_sz0_pr0", "ldc.l @rm+,dbr", 4, 0, PRIV, 0 },
	{ "0100mmmm11111010_sz0_pr0", "ldc rm,dbr", 0, 0, PRIV, 0 },
	{ "0100mmmm1nnn0111_sz0_pr0", "ldc.l @rm+,rn_bank", 4, 0, PRIV, 0 },
	{ "0100mmmm1nnn1110_sz0_pr0", "ldc rm,rn_bank", 0, 0, PRIV, 0 },
	{ "0100nnnn00000000_sz0_pr0", "shll rn", 0, 0, 0, 0 },
	{ "0100nnnn00000001_sz0_pr0", "shlr rn", 0, 0, 0, 0 },
	{ "0100nnnn00000010_sz0_pr0", "sts.l mach,@-rn", 0, 4, 0, 0 },
	{ "0100nnnn00000011_sz0_pr0", "stc.l sr,@-rn", 0, 4, PRIV, 0 },
	{ "0100nnnn00000100_sz0_pr0", "rotl rn", 0, 0, 0, 0 },
	{ "0100nnnn00000101_sz0_pr0", "rotr rn", 0, 0, 0, 0 },
	{ "0100nnnn00001000_sz0_pr0", "shll2 rn", 0, 0, 0, 0 },
	{ "0100nnnn00001001_sz0_pr0", "shlr2 rn", 0, 0, 0, 0 },
	{ "0100nnnn00010000_sz0_pr0", "dt rn", 0, 0, 0, 0 },
	{ "0100nnnn00010001_sz0_pr0", "cmp/pz rn", 0, 0, 0, 0 },
	{ "0100nnnn00010010_sz0_pr0", "sts.l macl,@-rn", 0, 4, 0, 0 },
	{ "0100nnnn00010011_sz0_pr0", "stc.l gbr,@-rn", 0, 4, 0, 0 },
	{ "0100nnnn00010101_sz0_pr0", "cmp/pl rn", 0, 0, 0, 0 },
	{ "0100nnnn00011000_sz0_pr0", "shll8 rn", 0, 0, 0, 0 },
	{ "0100nnnn00011001_sz0_pr0", "shlr8 rn", 0, 0, 0, 0 },
	{ "0100nnnn00011011_sz0_pr0", "tas.b @rn", 1, 1, 0, 0 },
	{ "0100nnnn00100000_sz0_pr0", "shal rn", 0, 0, 0, 0 },
	{ "0100nnnn00100001_sz0_pr0", "shar rn", 0, 0, 0, 0 },
	{ "0100nnnn00100010_sz0_pr0", "sts.l pr,@-rn", 0, 4, 0, 0 },
	{ "0100nnnn00100011_sz0_pr0", "stc.l vbr,@-rn", 0, 4, PRIV, 0 },
	{ "0100nnnn00100100_sz0_pr0", "rotcl rn", 0, 0, 0, 0 },
	{ "0100nnnn00100101_sz0_pr0", "rotcr rn", 0, 0, 0, 0 },
	{ "0100nnnn00101000_sz0_pr0", "shll16 rn", 0, 0, 0, 0 },
	{ "0100nnnn00101001_sz0_pr0", "shlr16 rn", 0, 0, 0, 0 },
	{ "0100nnnn00110010_sz0_pr0", "stc.l sgr,@-rn", 0, 4, PRIV, 0 },
	{ "0100nnnn00110011_sz0_pr0", "stc.l ssr,@-rn", 0, 4, PRIV, 0 },
	{ "0100nnnn01000011_sz0_pr0", "stc.l spc,@-rn", 0, 4, PRIV, 0 },
	{ "0100nnnn01010010_sz0_pr0", "sts.l fpul,@-rn", 0, 4, FPU, 0 },
	{ "0100nnnn01100010_sz0_pr0", "sts.l fpscr,@-rn", 0, 4, FPU, 0 },
	{ "0100nnnn11110010_sz0_pr0", "stc.l dbr,@-rn", 0, 4, PRIV, 0 },
	{ "0100nnnn1mmm0011_sz0_pr0", "stc.l rm_bank,@-rn", 0, 4, PRIV, 0 },
	{ "0100nnnnmmmm1100_sz0_pr0", "shad rm,rn", 0, 0, 0, 0 },
	{ "0100nnnnmmmm1101_sz0_pr0", "shld rm,rn", 0, 0, 0, 0 },
	{ "0101nnnnmmmmdddd_sz0_pr0", "mov.l @(disp,rm),rn", 4, 0, 0, 0 },
	{ "0110nnnnmmmm0000_sz0_pr0", "mov.b @rm,rn", 1, 0, 0, 0 },
	{ "0110nnnnmmmm0001_sz0_pr0", "mov.w @rm,rn", 2, 0, 0, 0 },
	{ "0110nnnnmmmm0010_sz0_pr0", "mov.l @rm,rn", 4, 0, 0, 0 },
	{ "0110nnnnmmmm0011_sz0_pr0", "mov rm,rn", 0, 0, 0, 0 },
	{ "0110nnnnmmmm0100_sz0_pr0", "mov.b @rm+,rn", 1, 0, 0, 0 },
	{ "0110nnnnmmmm0101_sz0_pr0", "mov.w @rm+,rn", 2, 0, 0, 0 },
	{ "0110nnnnmmmm0110_sz0_pr0", "mov.l @rm+,rn", 4, 0, 0, 0 },
	{ "0110nnnnmmmm0111_sz0_pr0", "not rm,rn", 0, 0, 0, 0 },
	{ "0110nnnnmmmm1000_sz0_pr0", "swap.b rm,rn", 0, 0, 0, 0 },
	{ "0110nnnnmmmm1001_sz0_pr0", "swap.w rm,rn", 0, 0, 0, 0 },
	{ "0110nnnnmmmm1010_sz0_pr0", "negc rm,rn", 0, 0, 0, 0 },
	{ "0110nnnnmmmm1011_sz0_pr0", "neg rm,rn", 0, 0, 0, 0 },
	{ "0110nnnnmmmm1100_sz0_pr0", "extu.b rm,rn", 0, 0, 0, 0 },
	{ "0110nnnnmmmm1101_sz0_pr0", "extu.w rm,rn", 0, 0, 0, 0 },
	{ "0110nnnnmmmm1110_sz0_pr0", "exts.b rm,rn", 0, 0, 0, 0 },
	{ "0110nnnnmmmm1111_sz0_pr0", "exts.w rm,rn", 0, 0, 0, 0 },
	{ "0111nnnniiiiiiii_sz0_pr0", "add #imm,rn", 0, 0, 0, 0 },
	{ "10000000nnnndddd_sz0_pr0", "mov.b r0,@(disp,rn)", 0, 1, 0, 0 },
	{ "10000001nnnndddd_sz0_pr0", "mov.w r0,@(disp,rn)", 0, 2, 0, 0 },
	{ "10000100mmmmdddd_sz0_pr0", "mov.b @(disp,rm),r0", 1, 0, 0, 0 },
	{ "10000101mmmmdddd_sz0_pr0", "mov.w @(disp,rm),r0", 2, 0, 0, 0 },
	{ "10001000iiiiiiii_sz0_pr0", "cmp/eq #imm,r0", 0, 0, 0, 0 },
	{ "10001001dddddddd_sz0_pr0", "bt disp", 0, 0, 0, 0 },
	{ "10001011dddddddd_sz0_pr0", "bf disp", 0, 0, 0, 0 },
	{ "10001101dddddddd_sz0_pr0", "bt/s disp", 0, 0, 0, 0 },
	{ "10001111dddddddd_sz0_pr0", "bf/s disp", 0, 0, 0, 0 },
	{ "1001nnnndddddddd_sz0_pr0", "mov.w @(disp,pc),rn", 2, 0, 0, 0 },
	{ "1010dddddddddddd_sz0_pr0", "bra disp", 0, 0, 0, 0 },
	{ "1011dddddddddddd_sz0_pr0", "bsr disp", 0, 0, 0, 0 },
	{ "11000000dddddddd_sz0_pr0", "mov.b r0,@(disp,gbr)", 0, 1, 0, 0 },
	{ "11000001dddddddd_sz0_pr0", "mov.w r0,@(disp,gbr)", 0, 2, 0, 0 },
	{ "11000010dddddddd_sz0_pr0", "mov.l r0,@(disp,gbr)", 0, 4, 0, 0 },
	{ "11000011iiiiiiii_sz0_pr0", "trapa #imm", 0, 0, SSP_TRAPA, 0 },
	{ "11000100dddddddd_sz0_pr0", "mov.b @(disp,gbr),r0", 1, 0, 0, 0 },
	{ "11000101dddddddd_sz0_pr0", "mov.w @(disp,gbr),r0", 2, 0, 0, 0 },
	{ "11000110dddddddd_sz0_pr0", "mov.l @(disp,gbr),r0", 4, 0, 0, 0 },
	{ "11000111dddddddd_sz0_pr0", "mova @(disp,pc),r0", 0, 0, 0, 0 },
	{ "11001000iiiiiiii_sz0_pr0", "tst #imm,r0", 0, 0, 0, 0 },
	{ "11001001iiiiiiii_sz0_pr0", "and #imm,r0", 0, 0, 0, 0 },
	{ "11001010iiiiiiii_sz0_pr0", "xor #imm,r0", 0, 0, 0, 0 },
	{ "11001011iiiiiiii_sz0_pr0", "or #imm,r0", 0, 0, 0, 0 },
	{ "11001100iiiiiiii_sz0_pr0", "tst.b #imm,@(r0,gbr)", 1, 0, 0, 0 },
	{ "11001101iiiiiiii_sz0_pr0", "and.b #imm,@(r0,gbr)", 1, 1, 0, 0 },
	{ "11001110iiiiiiii_sz0_pr0", "xor.b #imm,@(r0,gbr)", 1, 1, 0, 0 },
	{ "11001111iiiiiiii_sz0_pr0", "or.b #imm,@(r0,gbr)", 1, 1, 0, 0 },
	{ "1101nnnndddddddd_sz0_pr0", "mov.l @(disp,pc),rn", 4, 0, 0, 0 },
	{ "1110nnnniiiiiiii_sz0_pr0", "mov #imm,rn", 0, 0, 0, 0 },
	{ "1111001111111101_sz0_pr0", "fschg", 0, 0, FPU, 0 },
	{ "1111101111111101_sz0_pr0", "frchg", 0, 0, FPU, 0 },
	{ "1111mmm000111101_sz0_pr1", "ftrc drm,fpul", 0, 0, FPU | SSP_FTRC | SSP_DOUBLE, 0 },
	{ "1111mmm010111101_sz0_pr0", "fcnvds drm,fpul (pr=0)", 0, 0, FPU, 0 },
	{ "1111mmmm00011101_sz0_pr0", "flds frm,fpul", 0, 0, FPU, 0 },
	{ "1111mmmm00111101_sz0_pr0", "ftrc frm,fpul", 0, 0, FPU | SSP_FTRC, 0 },
	{ "1111nn0111111101_sz0_pr0", "ftrv xmtrx,fvn", 0, 0, FPR, known_sum_of_products },
	{ "1111nnmm11101101_sz0_pr0", "fipr fvm,fvn", 0, 0, FPR, known_sum_of_products },
	{ "1111nnn000101101_sz0_pr1", "float fpul,drn", 0, 0, FPU, 0 },
	{ "1111nnn001001101_sz0_pr1", "fneg drn", 0, 0, FPU, 0 },
	{ "1111nnn001011101_sz0_pr1", "fabs drn", 0, 0, FPU, 0 },
	{ "1111nnn001101101_sz0_pr1", "fsqrt drn", 0, 0, FPRD, 0 },
	{ "1111nnn010101101_sz0_pr0", "fcnvsd fpul,drn (pr=0)", 0, 0, FPU, 0 },
	{ "1111nnn011111101_sz0_pr0", "fsca fpul,drn", 0, 0, FPU, 0 },
	{ "1111nnn0mmm00000_sz0_pr1", "fadd drm,drn", 0, 0, FPRD, 0 },
	{ "1111nnn0mmm00001_sz0_pr1", "fsub drm,drn", 0, 0, FPRD, 0 },
	{ "1111nnn0mmm00010_sz0_pr1", "fmul drm,drn", 0, 0, FPRD, 0 },
	{ "1111nnn0mmm00011_sz0_pr1", "fdiv drm,drn", 0, 0, FPRD, 0 },
	{ "1111nnn0mmm00100_sz0_pr1", "fcmp/eq drm,drn", 0, 0, FPU, 0 },
	{ "1111nnn0mmm00101_sz0_pr1", "fcmp/gt drm,drn", 0, 0, FPU, 0 },
	{ "1111nnn0mmm01100_sz1_pr0", "fmov drm,drn", 0, 0, FPU, 0 },
	{ "1111nnn0mmm11100_sz1_pr0", "fmov xdm,drn", 0, 0, FPU, 0 },
	{ "1111nnn0mmmm0110_sz1_pr0", "fmov @(r0,rm),drn", 8, 0, FPU, 0 },
	{ "1111nnn0mmmm1000_sz1_pr0", "fmov @rm,drn", 8, 0, FPU, 0 },
	{ "1111nnn0mmmm1001_sz1_pr0", "fmov @rm+,drn", 8, 0, FPU, 0 },
	{ "1111nnn1mmm01100_sz1_pr0", "fmov drm,xdn", 0, 0, FPU, 0 },
	{ "1111nnn1mmm11100_sz1_pr0", "fmov xdm,xdn", 0, 0, FPU, 0 },
	{ "1111nnn1mmmm0110_sz1_pr0", "fmov @(r0,rm),xdn", 8, 0, FPU, 0 },
	{ "1111nnn1mmmm1000_sz1_pr0", "fmov @rm,xdn", 8, 0, FPU, 0 },
	{ "1111nnn1mmmm1001_sz1_pr0", "fmov @rm+,xdn", 8, 0, FPU, 0 },
	{ "1111nnnn00001101_sz0_pr0", "fsts fpul,frn", 0, 0, FPU, 0 },
	{ "1111nnnn00101101_sz0_pr0", "float fpul,frn", 0, 0, FPU, 0 },
	{ "1111nnnn01001101_sz0_pr0", "fneg frn", 0, 0, FPU, 0 },
	{ "1111nnnn01011101_sz0_pr0", "fabs frn", 0, 0, FPU, 0 },
	{ "1111nnnn01101101_sz0_pr0", "fsqrt frn", 0, 0, FPR, 0 },
	{ "1111nnnn01111101_sz0_pr0", "fsrra frn", 0, 0, FPR, 0 },
	{ "1111nnnn10001101_sz0_pr0", "fldi0 frn", 0, 0, FPU, 0 },
	{ "1111nnnn10011101_sz0_pr0", "fldi1 frn", 0, 0, FPU, 0 },
	{ "1111nnnnmmm00111_sz1_pr0", "fmov drm,@(r0,rn)", 0, 8, FPU, 0 },
	{ "1111nnnnmmm01010_sz1_pr0", "fmov drm,@rn", 0, 8, FPU, 0 },
	{ "1111nnnnmmm01011_sz1_pr0", "fmov drm,@-rn", 0, 8, FPU, 0 },
	{ "1111nnnnmmm10111_sz1_pr0", "fmov xdm,@(r0,rn)", 0, 8, FPU, 0 },
	{ "1111nnnnmmm11010_sz1_pr0", "fmov xdm,@rn", 0, 8, FPU, 0 },
	{ "1111nnnnmmm11011_sz1_pr0", "fmov xdm,@-rn", 0, 8, FPU, 0 },
	{ "1111nnnnmmmm0000_sz0_pr0", "fadd frm,frn", 0, 0, FPR, 0 },
	{ "1111nnnnmmmm0001_sz0_pr0", "fsub frm,frn", 0, 0, FPR, 0 },
	{ "1111nnnnmmmm0010_sz0_pr0", "fmul frm,frn", 0, 0, FPR, 0 },
	{ "1111nnnnmmmm0011_sz0_pr0", "fdiv frm,frn", 0, 0, FPR, 0 },
	{ "1111nnnnmmmm0100_sz0_pr0", "fcmp/eq frm,frn", 0, 0, FPU, 0 },
	{ "1111nnnnmmmm0101_sz0_pr0", "fcmp/gt frm,frn", 0, 0, FPU, 0 },
	{ "1111nnnnmmmm0110_sz0_pr0", "fmov.s @(r0,rm),frn", 4, 0, FPU, 0 },
	{ "1111nnnnmmmm0111_sz0_pr0", "fmov.s frm,@(r0,rn)", 0, 4, FPU, 0 },
	{ "1111nnnnmmmm1000_sz0_pr0", "fmov.s @rm,frn", 4, 0, FPU, 0 },
	{ "1111nnnnmmmm1001_sz0_pr0", "fmov.s @rm+,frn", 4, 0, FPU, 0 },
	{ "1111nnnnmmmm1010_sz0_pr0", "fmov.s frm,@rn", 0, 4, FPU, 0 },
	{ "1111nnnnmmmm1011_sz0_pr0", "fmov.s frm,@-rn", 0, 4, FPU, 0 },
	{ "1111nnnnmmmm1100_sz0_pr0", "fmov frm,frn", 0, 0, FPU, 0 },
	{ "1111nnnnmmmm1110_sz0_pr0", "fmac fr0,frm,frn", 0, 0, FPR, 0 },
};

#undef FPU
#undef PRIV
#undef FPR
#undef FPRD

u32 ss_policy_count()
{
	return sizeof(policy) / sizeof(policy[0]);
}

const SsFilePolicy* ss_policy_at(u32 index)
{
	return index < ss_policy_count() ? &policy[index] : 0;
}

const SsFilePolicy* ss_policy_find(const std::string& file)
{
	for (u32 i = 0; i < ss_policy_count(); i++)
	{
		if (file == policy[i].file)
			return &policy[i];
	}

	return 0;
}

//The bits FPSCR has: FR SZ PR DN, cause, enable, flag, RM. The other 10 read 0 and are not written
#define SS_FPSCR_BITS 0x003FFFFF

static bool is_nan32(u32 v)
{
	return (v & 0x7F800000) == 0x7F800000 && (v & 0x007FFFFF) != 0;
}

static bool is_nan64(u32 high, u32 low)
{
	return (high & 0x7FF00000) == 0x7FF00000 && ((high & 0x000FFFFF) | low) != 0;
}

//The instruction leaves a NaN in a register of the active FP bank that it writes
static bool result_is_nan(const SsFilePolicy& policy, const SsTest& test)
{
	const u32* before = test.initial.s.FP0;
	const u32* after = test.final_.s.FP0;

	if (policy.flags & SSP_DOUBLE)
	{
		for (u32 i = 0; i < 16; i += 2)
		{
			if ((before[i] != after[i] || before[i + 1] != after[i + 1]) && is_nan64(after[i], after[i + 1]))
				return true;
		}
	}
	else
	{
		for (u32 i = 0; i < 16; i++)
		{
			if (before[i] != after[i] && is_nan32(after[i]))
				return true;
		}
	}

	return false;
}

//FTRC of a NaN that is not expected to give what an SH4 gives for every NaN
static bool ftrc_of_nan(const SsFilePolicy& policy, const SsTest& test)
{
	const u32* fp = test.initial.s.FP0;
	u32 m = (ss_opcode(test) >> 8) & 0xF;

	if (test.final_.s.FPUL == 0x80000000)
		return false;

	if (policy.flags & SSP_DOUBLE)
		return is_nan64(fp[m & 0xE], fp[(m & 0xE) + 1]);

	return is_nan32(fp[m]);
}

//Single precision FTRC of 2^31 or more (+inf too) that is not expected to give what an SH4 gives. The generator
//cuts the operand to 2147483520.0f first, whose integer is 7FFFFF80
static bool ftrc_above_range(const SsFilePolicy& policy, const SsTest& test)
{
	u32 operand = test.initial.s.FP0[(ss_opcode(test) >> 8) & 0xF];

	if ((policy.flags & SSP_DOUBLE) || test.final_.s.FPUL == 0x7FFFFFFF)
		return false;

	return operand >= 0x4F000000 && operand <= 0x7F800000;
}

//R0-R7 are banked by SR.RB in privileged mode, user mode has bank 0
static bool sr_bank(u32 sr)
{
	return (sr & 0x60000000) == 0x60000000;
}

//RTE to the other register bank, and the add r1,r1 of the delay slot is expected to have doubled R1 of the bank
//RTE is entered with. After the change that bank is the inactive one, R_ in a vector
static bool rte_slot_on_old_bank(const SsTest& test)
{
	const SsState& before = test.initial.s;
	const SsState& after = test.final_.s;

	if (sr_bank(before.SR) == sr_bank(before.SSR) || test.opcodes.op[SS_TEST_INSTRUCTION + 1] != 0x311C)
		return false;

	bool old_bank = after.R_[1] == before.R[1] * 2 && after.R[1] == before.R_[1];
	bool new_bank = after.R[1] == before.R_[1] * 2 && after.R_[1] == before.R[1];

	return old_bank && !new_bank;
}

//TRAPA as the single-step vectors have it: SR stays and the instruction behind it is run. A TRAPA that is taken
//goes to the handler with SR.BL set, or resets the CPU when SR.BL was set
static bool trapa_recorded_as_nop(const SsTest& test)
{
	return test.final_.s.SR == test.initial.s.SR && test.cycles.c[SS_TEST_INSTRUCTION + 1].fetch_addr == test.initial.s.PC + 4;
}

SsFilterReason ss_policy_filter(const SsFilePolicy& policy, const SsTest& test, u32 ignore)
{
	const SsState& initial = test.initial.s;

#define APPLIES(reason) ((ignore & (1 << (reason))) == 0)

	if (APPLIES(SSF_TRAPA_NOP) && (policy.flags & SSP_TRAPA) && trapa_recorded_as_nop(test))
		return SSF_TRAPA_NOP;

	//exceptions of the instruction itself come before the ones of its data access
	if (APPLIES(SSF_PRIVILEGED) && (policy.flags & SSP_PRIVILEGED) && (initial.SR & 0x40000000) == 0)
		return SSF_PRIVILEGED;

	if (APPLIES(SSF_FPU_DISABLED) && (policy.flags & SSP_FPU) && (initial.SR & 0x00008000) != 0)
		return SSF_FPU_DISABLED;

	for (u32 i = 0; i < 4 && APPLIES(SSF_UNALIGNED); i++)
	{
		const SsCycle& c = test.cycles.c[i];

		if ((c.actions & SS_ACTION_READ) && policy.read_size > 1 && (c.read_addr & (policy.read_size - 1)) != 0)
			return SSF_UNALIGNED;

		if ((c.actions & SS_ACTION_WRITE) && policy.write_size > 1 && (c.write_addr & (policy.write_size - 1)) != 0)
			return SSF_UNALIGNED;
	}

	for (u32 i = 0; i < 4 && APPLIES(SSF_ODD_PC); i++)
	{
		const SsCycle& c = test.cycles.c[i];

		if ((c.actions & SS_ACTION_FETCH) && (c.fetch_addr & 1) != 0)
			return SSF_ODD_PC;
	}

	if (APPLIES(SSF_HOST_NAN) && (policy.flags & SSP_FP_RESULT) && result_is_nan(policy, test))
		return SSF_HOST_NAN;

	if (APPLIES(SSF_HOST_NAN) && (policy.flags & SSP_FTRC) && ftrc_of_nan(policy, test))
		return SSF_HOST_NAN;

	if (APPLIES(SSF_FTRC_RANGE) && (policy.flags & SSP_FTRC) && ftrc_above_range(policy, test))
		return SSF_FTRC_RANGE;

	if (APPLIES(SSF_FPSCR_BITS) && (test.final_.s.FPSCR & ~SS_FPSCR_BITS) != 0)
		return SSF_FPSCR_BITS;

	if (APPLIES(SSF_RTE_BANK) && (policy.flags & SSP_RTE) && rte_slot_on_old_bank(test))
		return SSF_RTE_BANK;

#undef APPLIES

	return SSF_NONE;
}

const char* ss_policy_notification(const SsFilePolicy& policy, const SsTest& test)
{
	//RaiseFPUDisableException of the NO_MMU build, in front of the instruction that is then executed. Only the
	//vectors that are run with --unfilter fpu-disabled get here, and all vectors of the differential
	if ((policy.flags & SSP_FPU) && (test.initial.s.SR & 0x00008000) != 0)
		return "Full MMU support needed";

	return 0;
}

const char* ss_filter_name(SsFilterReason reason)
{
	switch (reason)
	{
	case SSF_NONE:         return "run";
	case SSF_UNALIGNED:    return "unaligned";
	case SSF_ODD_PC:       return "odd-pc";
	case SSF_FPU_DISABLED: return "fpu-disabled";
	case SSF_PRIVILEGED:   return "privileged";
	case SSF_TRAPA_NOP:    return "trapa-nop";
	case SSF_HOST_NAN:     return "host-nan";
	case SSF_FPSCR_BITS:   return "fpscr-bits";
	case SSF_RTE_BANK:     return "rte-bank";
	case SSF_FTRC_RANGE:   return "ftrc-range";
	default:               return "?";
	}
}

const char* ss_filter_description(SsFilterReason reason)
{
	switch (reason)
	{
	case SSF_UNALIGNED:
		return "16/32/64 bit data access at an address that is not a multiple of its width. The vectors access "
		       "unaligned memory freely, an SH4 raises an address error and nullDC ends in its unaligned access handler";
	case SSF_ODD_PC:
		return "instruction fetch from an odd address (jump through a register with bit 0 set). The vectors fetch from it, "
		       "an SH4 raises an address error";
	case SSF_FPU_DISABLED:
		return "FPU instruction with SR.FD=1. The vectors execute it, an SH4 raises an FPU disable exception";
	case SSF_PRIVILEGED:
		return "privileged instruction with SR.MD=0. The vectors execute it, an SH4 raises an illegal instruction exception";
	case SSF_TRAPA_NOP:
		return "TRAPA is recorded as a nop: no SSR/SPC/SR/TRA/EXPEVT change, PC+2. An SH4 takes the TRAPA exception, or "
		       "is reset by it when SR.BL=1";
	case SSF_HOST_NAN:
		return "the instruction produces a NaN (or converts one with FTRC). The expected value is the default NaN of the "
		       "ARM64 host that generated the vectors (FTRC of a NaN: 0), not the SH4's (FTRC of a NaN: 80000000)";
	case SSF_FPSCR_BITS:
		return "the vector expects unimplemented FPSCR bits stored (LDS / LDS.L to FPSCR of a value with bits 22-31 set). "
		       "An SH4 has the low 22 bits only, the others read 0";
	case SSF_RTE_BANK:
		return "RTE to the other register bank: the vector runs the delay slot on the bank of the old SR. An SH4 runs it "
		       "with the SR that RTE restores, on the new bank";
	case SSF_FTRC_RANGE:
		return "single precision FTRC of 2^31 or more, +inf too. The vector has 7FFFFF80: its generator cuts the operand to "
		       "2147483520.0f first. An SH4 gives 7FFFFFFF";
	default:
		return "";
	}
}
