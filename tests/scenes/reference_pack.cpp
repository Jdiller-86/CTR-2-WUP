/* Original test adapter for the unmodified, external BSD nihstro header.
 * Never linked into CTR2WUP or vendored in delivery. */
#include <nihstro/shader_bytecode.h>
#include <cstdio>

static bool emit(unsigned value) {
    unsigned char bytes[4];for(unsigned i=0;i<4;++i)bytes[i]=value>>(i*8);
    return std::fwrite(bytes,1,4,stdout)==4;
}
int main() {
    for(unsigned src=0;src<128;++src)for(unsigned dest=0;dest<32;++dest) {
        nihstro::Instruction instruction;instruction.hex=0;
        instruction.opcode=nihstro::OpCode::Id::MOV;
        instruction.common.src1=nihstro::SourceRegister(src);
        instruction.common.dest=nihstro::DestRegister(dest);
        instruction.common.operand_desc_id=(src+dest)&127;
        if(!emit(instruction.hex))return 1;
    }
    for(unsigned selectors=0;selectors<256;++selectors)for(unsigned negate=0;negate<2;++negate)
        for(unsigned mask=0;mask<16;++mask) {
            nihstro::SwizzlePattern pattern;pattern.hex=0;
            pattern.src1_selector=selectors;pattern.negate_src1=negate;pattern.dest_mask=mask;
            if(!emit(pattern.hex))return 2;
        }
    for(auto op:{nihstro::OpCode::Id::NOP,nihstro::OpCode::Id::END}) {
        nihstro::Instruction instruction;instruction.hex=0;instruction.opcode=op;
        if(!emit(instruction.hex))return 3;
    }
    return std::ferror(stdout)?4:0;
}
