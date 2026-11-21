// SpectraFX: CPU-side preparation of the bounded, opt-in Metal VM ABI.
// A successful prepare() only validates the representable program subset;
// actual GPU execution additionally needs the matching Metal compute kernel,
// persistent particle-register storage and a compatible RHI device.
// Unsupported complete effects MUST retain the full CPU fallback.
#pragma once
#include "SpectraParticleProgram.h"
#include <array>
#include <cstdint>
#include <string>

namespace aiverse::spectra::gpu_program {

inline constexpr uint32_t kMaxStatements = 64;
inline constexpr uint32_t kMaxInstructions = 512;
inline constexpr uint32_t kAbiVersion = 1;

// Four-byte enum values make the payload independent of C++ enum layout.
// The consumer MUST validate abiVersion before binding any GPU buffer.
struct Instruction {
    uint32_t opcode = 0;
    uint32_t field = 0;
    float scalar = 0.f;
    uint32_t reserved = 0;
};
struct Statement {
    uint32_t stage = 0; // 0=Spawn, 1=Update
    uint32_t action = 0;
    uint32_t target = 0;
    uint32_t firstInstruction = 0;
    uint32_t instructionCount = 0;
    uint32_t reserved[3] = {};
};
struct Payload {
    uint32_t abiVersion = kAbiVersion;
    uint32_t statementCount = 0;
    uint32_t instructionCount = 0;
    uint32_t parameterMask = 0;
    std::array<Statement,kMaxStatements> statements{};
    std::array<Instruction,kMaxInstructions> instructions{};
};
static_assert(sizeof(Instruction)==16);
static_assert(sizeof(Statement)==32);
static_assert(sizeof(Payload)==16+64*32+512*16,
              "Metal VM payload layout changed: update the MSL ABI");
static_assert(uint32_t(program::Field::Age)==0 &&
              uint32_t(program::Field::PositionX)==4 &&
              uint32_t(program::Field::VelocityX)==7 &&
              uint32_t(program::Field::TintA)==15 &&
              uint32_t(program::Field::Attribute0)==16 &&
              uint32_t(program::Field::Attribute7)==23 &&
              uint32_t(program::Field::Param0)==24 &&
              uint32_t(program::Field::Param7)==31,
              "Metal VM field IDs changed: update vmRead/vmWrite");
static_assert(uint32_t(program::Op::Constant)==0 &&
              uint32_t(program::Op::Read)==1 &&
              uint32_t(program::Op::Clamp)==17 &&
              uint32_t(program::Op::Lerp)==18,
              "Metal VM opcode IDs changed: update vmEvaluate");
static_assert(uint32_t(program::Action::Set)==0 &&
              uint32_t(program::Action::Add)==1 &&
              uint32_t(program::Action::AddRate)==2 &&
              uint32_t(program::Action::Multiply)==3,
              "Metal VM actions changed: update vmRun");

// The Metal VM has a separate per-physical-slot 32-byte attribute buffer.
// Rotation/uid and event-only stages still require additional GPU semantics.
inline bool supportedField(program::Field field, bool target) {
    using F=program::Field;
    switch(field) {
    case F::PositionX: case F::PositionY: case F::PositionZ:
    case F::VelocityX: case F::VelocityY: case F::VelocityZ:
    case F::Size:
    case F::TintR: case F::TintG: case F::TintB: case F::TintA:
    case F::Attribute0: case F::Attribute1: case F::Attribute2: case F::Attribute3:
    case F::Attribute4: case F::Attribute5: case F::Attribute6: case F::Attribute7:
        return true;
    case F::Age: case F::AgeNormalized: case F::Dt:
    case F::Param0: case F::Param1: case F::Param2: case F::Param3:
    case F::Param4: case F::Param5: case F::Param6: case F::Param7:
        return !target;
    default: return false;
    }
}
inline bool supportedOp(program::Op op) {
    using O=program::Op;
    switch(op) {
    case O::Constant: case O::Read: case O::Add: case O::Subtract:
    case O::Multiply: case O::Min: case O::Max: case O::Abs:
    case O::Negate: case O::Saturate: case O::Clamp: case O::Lerp:
        return true;
    default: return false;
    }
}

inline bool prepare(const program::Program& source, Payload& output,
                    std::string& reason) {
    Payload next{};
    reason.clear();
    const auto reject=[&](const char* why) {
        reason=why;
        output=Payload{}; // Never expose a partial/usable payload on error.
        return false;
    };
    for(size_t stage=2;stage<size_t(program::Stage::Count);++stage) {
        if(!source.stages[stage].empty())
            return reject("GPU VM payload does not support collision, trigger or death stages");
    }
    for(uint32_t stage=0;stage<2;++stage) {
        for(const auto& statement:source.stages[stage]) {
            if(next.statementCount>=kMaxStatements)
                return reject("GPU VM statement budget exceeded");
            if(!supportedField(statement.field,true))
                return reject("GPU VM unsupported output field");
            if(statement.code.empty())
                return reject("GPU VM empty expression");
            if(statement.code.size()>kMaxInstructions-next.instructionCount)
                return reject("GPU VM instruction budget exceeded");
            Statement s{};
            s.stage=stage;
            s.action=uint32_t(statement.action);
            s.target=uint32_t(statement.field);
            s.firstInstruction=next.instructionCount;
            s.instructionCount=uint32_t(statement.code.size());
            if(s.action>uint32_t(program::Action::Multiply))
                return reject("GPU VM unsupported assignment action");
            for(const auto& ins:statement.code) {
                if(!supportedOp(ins.op))
                    return reject("GPU VM unsupported arithmetic instruction");
                if(ins.op==program::Op::Read && !supportedField(ins.field,false))
                    return reject("GPU VM unsupported input field");
                if(ins.op==program::Op::Constant && !program::number(ins.constant))
                    return reject("GPU VM constant is not finite or bounded");
                if(ins.op==program::Op::Read &&
                   ins.field>=program::Field::Param0 && ins.field<=program::Field::Param7)
                    next.parameterMask|=uint32_t(1)<<(uint32_t(ins.field)-uint32_t(program::Field::Param0));
                next.instructions[next.instructionCount++]={
                    uint32_t(ins.op), uint32_t(ins.field), ins.constant, 0
                };
            }
            next.statements[next.statementCount++]=s;
        }
    }
    output=next;
    return true;
}

// Stage 22 executable Metal-subset validation. The ABI can represent more
// instructions than the first GPU interpreter is allowed to evaluate.
// This separate gate prevents a program from being partially executed.
inline bool prepareForMetal(const Json& json,Payload& output,std::string& reason) {
    output=Payload{};
    if(json.isNull()) {reason.clear();return true;}
    program::Program source;
    std::vector<std::string> errors;
    if(!program::compile(json,source,errors)) {
        reason=errors.empty()?"invalid CPU particle program":errors.front();
        return false;
    }
    if(!prepare(source,output,reason))return false;
    if(output.statementCount>24 || output.instructionCount>192) {
        reason="GPU VM executable budget: at most 24 statements and 192 instructions";
        output=Payload{};
        return false;
    }
    for(uint32_t i=0;i<output.statementCount;++i) {
        const Statement& st=output.statements[i];
        if(st.instructionCount>48 || st.firstInstruction+st.instructionCount>
           output.instructionCount) {
            reason="GPU VM executable statement exceeds 48 instructions";
            output=Payload{};
            return false;
        }
    }
    reason.clear();
    return true;
}

} // namespace aiverse::spectra::gpu_program
