// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>
#include <boost/container/small_vector.hpp>
#include <nihstro/shader_bytecode.h>
#include <sirit/sirit.h>

#include "common/assert.h"
#include "video_core/pica/regs_rasterizer.h"
#include "video_core/pica/shader_setup.h"
#include "video_core/shader/generator/shader_gen.h"
#include "video_core/shader/generator/spv_vs_shader_gen.h"

namespace Pica::Shader::Generator::SPIRV {

using Sirit::Id;
using nihstro::DestRegister;
using nihstro::Instruction;
using nihstro::OpCode;
using nihstro::RegisterType;
using nihstro::SourceRegister;
using nihstro::SwizzlePattern;

using VSOutputAttributes = Pica::RasterizerRegs::VSOutputAttributes;

constexpr u32 SPIRV_VERSION_1_3 = 0x00010300;

bool CanGenerateVertexShader(const Pica::ProgramCode& program_code, u32 main_offset) {
    if (main_offset >= Pica::MAX_PROGRAM_CODE_LENGTH) {
        return false;
    }
    for (u32 offset = main_offset; offset < Pica::MAX_PROGRAM_CODE_LENGTH; ++offset) {
        const Instruction instr = {program_code[offset]};
        const OpCode::Info& info = instr.opcode.Value().GetInfo();
        switch (info.type) {
        case OpCode::Type::Trivial:
            if (instr.opcode.Value() == OpCode::Id::END) {
                return true;
            }
            if (instr.opcode.Value() != OpCode::Id::NOP) {
                return false;
            }
            break;
        case OpCode::Type::Arithmetic: {
            switch (instr.opcode.Value().EffectiveOpCode()) {
            case OpCode::Id::ADD:
            case OpCode::Id::MUL:
            case OpCode::Id::FLR:
            case OpCode::Id::MAX:
            case OpCode::Id::MIN:
            case OpCode::Id::DP3:
            case OpCode::Id::DP4:
            case OpCode::Id::DPH:
            case OpCode::Id::DPHI:
            case OpCode::Id::RCP:
            case OpCode::Id::RSQ:
            case OpCode::Id::MOVA:
            case OpCode::Id::MOV:
            case OpCode::Id::SGE:
            case OpCode::Id::SGEI:
            case OpCode::Id::SLT:
            case OpCode::Id::SLTI:
            case OpCode::Id::CMP:
            case OpCode::Id::EX2:
            case OpCode::Id::LG2:
                break;
            default:
                return false;
            }
            if (instr.common.address_register_index.Value() == 3) {
                return false;
            }
            break;
        }
        case OpCode::Type::MultiplyAdd:
            if (instr.mad.address_register_index.Value() == 3) {
                return false;
            }
            break;
        default:
            // Conditional / UniformFlowControl / SetEmit -- all imply control flow this
            // generator does not support.
            return false;
        }
    }
    return false;
}

namespace {

std::array<bool, 16> ScanUsedInputRegisters(const Pica::ProgramCode& program_code,
                                             u32 main_offset) {
    std::array<bool, 16> used{};
    const auto mark = [&](const SourceRegister& reg) {
        if (reg.GetRegisterType() == RegisterType::Input) {
            used[static_cast<u32>(reg.GetIndex())] = true;
        }
    };
    for (u32 offset = main_offset; offset < Pica::MAX_PROGRAM_CODE_LENGTH; ++offset) {
        const Instruction instr = {program_code[offset]};
        if (instr.opcode.Value() == OpCode::Id::END) {
            break;
        }
        const OpCode::Info& info = instr.opcode.Value().GetInfo();
        if (info.type == OpCode::Type::Arithmetic) {
            const bool is_inverted = (info.subtype & OpCode::Info::SrcInversed) != 0;
            mark(instr.common.GetSrc1(is_inverted));
            mark(instr.common.GetSrc2(is_inverted));
        } else if (info.type == OpCode::Type::MultiplyAdd) {
            const bool is_inverted =
                instr.opcode.Value().EffectiveOpCode() == OpCode::Id::MADI;
            mark(instr.mad.GetSrc1(is_inverted));
            mark(instr.mad.GetSrc2(is_inverted));
            mark(instr.mad.GetSrc3(is_inverted));
        }
    }
    return used;
}

class VertexModule : public Sirit::Module {
public:
    explicit VertexModule(const Pica::ShaderSetup& setup_, const PicaVSConfig& config_,
                          const ExtraVSConfig& extra_)
        : Sirit::Module{SPIRV_VERSION_1_3}, setup{setup_}, config{config_}, extra{extra_},
          used_regs{ScanUsedInputRegisters(setup_.GetProgramCode(), config_.state.main_offset)} {
        DefineArithmeticTypes();
        DefineUniformStructs();
        DefineInterface();
        DefineEntryPoint();
    }

    ~VertexModule() = default;

    void Generate() {
        AddLabel(OpLabel());

        for (u32 i = 0; i < 16; ++i) {
            if (!used_regs[i]) {
                continue;
            }
            Id raw_type = vec4_id;
            if (True(extra.load_flags[i] & AttribLoadFlags::Sint)) {
                raw_type = ivec4_id;
            } else if (True(extra.load_flags[i] & AttribLoadFlags::Uint)) {
                raw_type = uvec4_id;
            }
            const Id loaded{OpLoad(raw_type, input_var_ids[i])};
            Id converted = loaded;
            if (True(extra.load_flags[i] & AttribLoadFlags::Sint)) {
                converted = OpConvertSToF(vec4_id, loaded);
            } else if (True(extra.load_flags[i] & AttribLoadFlags::Uint)) {
                converted = OpConvertUToF(vec4_id, loaded);
            }
            if (True(extra.load_flags[i] & AttribLoadFlags::ZeroW)) {
                converted = OpCompositeInsert(vec4_id, ConstF32(0.f), converted, 3);
            }
            input_val[i] = converted;
        }

        for (u32 i = 0; i < 16; ++i) {
            reg_ids[i] = DefineVar<false>(vec4_id, spv::StorageClass::Function);
            OpStore(reg_ids[i], ConstF32(0.f, 0.f, 0.f, 1.f));
        }
        addr_reg_id = DefineVar<false>(ivec2_id, spv::StorageClass::Function);
        OpStore(addr_reg_id, ConstS32(0, 0));

        for (u32 i = 0; i < config.state.num_outputs; ++i) {
            out_reg_ids[i] = DefineVar<false>(vec4_id, spv::StorageClass::Function);
            OpStore(out_reg_ids[i], ConstF32(0.f, 0.f, 0.f, 1.f));
        }

        const auto& program_code = setup.GetProgramCode();
        for (u32 offset = config.state.main_offset;; ++offset) {
            const Instruction instr = {program_code[offset]};
            if (instr.opcode.Value() == OpCode::Id::END) {
                break;
            }
            const OpCode::Info& info = instr.opcode.Value().GetInfo();
            if (info.type == OpCode::Type::MultiplyAdd) {
                CompileMad(instr);
            } else if (info.type == OpCode::Type::Arithmetic) {
                CompileArithmetic(instr, info);
            }
        }

        WriteOutputs();

        OpReturn();
        OpFunctionEnd();
    }

private:
    template <bool global = true>
    [[nodiscard]] Id DefineVar(Id type, spv::StorageClass storage_class) {
        const Id pointer_type_id{TypePointer(storage_class, type)};
        return global ? AddGlobalVariable(pointer_type_id, storage_class)
                      : AddLocalVariable(pointer_type_id, storage_class);
    }

    [[nodiscard]] Id DefineInput(Id type, u32 location) {
        const Id input_id{DefineVar(type, spv::StorageClass::Input)};
        Decorate(input_id, spv::Decoration::Location, location);
        return input_id;
    }

    [[nodiscard]] Id DefineOutput(Id type, u32 location) {
        const Id output_id{DefineVar(type, spv::StorageClass::Output)};
        Decorate(output_id, spv::Decoration::Location, location);
        return output_id;
    }

    [[nodiscard]] Id ConstF32(f32 value) {
        return Constant(f32_id, value);
    }

    [[nodiscard]] Id ConstF32(f32 x, f32 y, f32 z) {
        const std::array<Id, 3> c{Constant(f32_id, x), Constant(f32_id, y), Constant(f32_id, z)};
        return ConstantComposite(vec3_id, c);
    }

    [[nodiscard]] Id ConstF32(f32 x, f32 y, f32 z, f32 w) {
        const std::array<Id, 4> c{Constant(f32_id, x), Constant(f32_id, y), Constant(f32_id, z),
                                  Constant(f32_id, w)};
        return ConstantComposite(vec4_id, c);
    }

    [[nodiscard]] Id ConstS32(s32 value) {
        return Constant(i32_id, value);
    }

    [[nodiscard]] Id ConstS32(s32 x, s32 y) {
        const std::array<Id, 2> c{Constant(i32_id, x), Constant(i32_id, y)};
        return ConstantComposite(ivec2_id, c);
    }

    [[nodiscard]] Id ConstU32(u32 value) {
        return Constant(u32_id, value);
    }

    void DefineArithmeticTypes() {
        void_id = TypeVoid();
        bool_id = TypeBool();
        f32_id = TypeFloat(32);
        i32_id = TypeSInt(32);
        u32_id = TypeUInt(32);
        vec2_id = TypeVector(f32_id, 2);
        vec3_id = TypeVector(f32_id, 3);
        vec4_id = TypeVector(f32_id, 4);
        ivec2_id = TypeVector(i32_id, 2);
        ivec4_id = TypeVector(i32_id, 4);
        uvec4_id = TypeVector(u32_id, 4);
        bvec4_id = TypeVector(bool_id, 4);
    }

    void DefineUniformStructs() {
        const Id i_array_id{TypeArray(uvec4_id, ConstU32(4u))};
        const Id f_array_id{TypeArray(vec4_id, ConstU32(96u))};
        Decorate(i_array_id, spv::Decoration::ArrayStride, 16u);
        Decorate(f_array_id, spv::Decoration::ArrayStride, 16u);

        const Id vs_pica_data_struct_id{TypeStruct(u32_id, i_array_id, f_array_id)};
        MemberDecorate(vs_pica_data_struct_id, 0u, spv::Decoration::Offset, 0u);
        MemberDecorate(vs_pica_data_struct_id, 1u, spv::Decoration::Offset, 16u);
        MemberDecorate(vs_pica_data_struct_id, 2u, spv::Decoration::Offset, 80u);
        Decorate(vs_pica_data_struct_id, spv::Decoration::Block);

        vs_pica_data_id = AddGlobalVariable(
            TypePointer(spv::StorageClass::Uniform, vs_pica_data_struct_id),
            spv::StorageClass::Uniform);
        Decorate(vs_pica_data_id, spv::Decoration::DescriptorSet, 0u);
        Decorate(vs_pica_data_id, spv::Decoration::Binding, 0u);

        const Id vs_data_struct_id{TypeStruct(u32_id, u32_id, vec4_id)};
        MemberDecorate(vs_data_struct_id, 0u, spv::Decoration::Offset, 0u);
        MemberDecorate(vs_data_struct_id, 1u, spv::Decoration::Offset, 4u);
        MemberDecorate(vs_data_struct_id, 2u, spv::Decoration::Offset, 16u);
        Decorate(vs_data_struct_id, spv::Decoration::Block);

        vs_data_id = AddGlobalVariable(TypePointer(spv::StorageClass::Uniform, vs_data_struct_id),
                                       spv::StorageClass::Uniform);
        Decorate(vs_data_id, spv::Decoration::DescriptorSet, 0u);
        Decorate(vs_data_id, spv::Decoration::Binding, 1u);
    }

    void DefineInterface() {
        primary_color_id = DefineOutput(vec4_id, ATTRIBUTE_COLOR);
        texcoord0_id = DefineOutput(vec2_id, ATTRIBUTE_TEXCOORD0);
        texcoord1_id = DefineOutput(vec2_id, ATTRIBUTE_TEXCOORD1);
        texcoord2_id = DefineOutput(vec2_id, ATTRIBUTE_TEXCOORD2);
        texcoord0_w_id = DefineOutput(f32_id, ATTRIBUTE_TEXCOORD0_W);
        normquat_id = DefineOutput(vec4_id, ATTRIBUTE_NORMQUAT);
        view_id = DefineOutput(vec3_id, ATTRIBUTE_VIEW);

        gl_position_id = DefineVar(vec4_id, spv::StorageClass::Output);
        Decorate(gl_position_id, spv::Decoration::BuiltIn, spv::BuiltIn::Position);

        if (extra.use_clip_planes) {
            const Id clip_array_id{TypeArray(f32_id, ConstU32(2u))};
            gl_clip_distance_id = DefineVar(clip_array_id, spv::StorageClass::Output);
            Decorate(gl_clip_distance_id, spv::Decoration::BuiltIn, spv::BuiltIn::ClipDistance);
        }

        for (u32 i = 0; i < 16; ++i) {
            if (!used_regs[i]) {
                continue;
            }
            Id raw_type = vec4_id;
            if (True(extra.load_flags[i] & AttribLoadFlags::Sint)) {
                raw_type = ivec4_id;
            } else if (True(extra.load_flags[i] & AttribLoadFlags::Uint)) {
                raw_type = uvec4_id;
            }
            input_var_ids[i] = DefineInput(raw_type, i);
        }
    }

    void DefineEntryPoint() {
        AddCapability(spv::Capability::Shader);
        SetMemoryModel(spv::AddressingModel::Logical, spv::MemoryModel::GLSL450);

        const Id main_type{TypeFunction(TypeVoid())};
        const Id main_func{OpFunction(TypeVoid(), spv::FunctionControlMask::MaskNone, main_type)};

        boost::container::small_vector<Id, 24> interface_ids{
            primary_color_id, texcoord0_id, texcoord1_id, texcoord2_id,
            texcoord0_w_id,   normquat_id,  view_id,      gl_position_id,
        };
        if (extra.use_clip_planes) {
            interface_ids.push_back(gl_clip_distance_id);
        }
        for (u32 i = 0; i < 16; ++i) {
            if (used_regs[i]) {
                interface_ids.push_back(input_var_ids[i]);
            }
        }

        AddEntryPoint(spv::ExecutionModel::Vertex, main_func, "main", interface_ids);
    }

    [[nodiscard]] Id GetFloatUniformConst(u32 index) {
        const Id ptr_type{TypePointer(spv::StorageClass::Uniform, vec4_id)};
        return OpLoad(vec4_id,
                      OpAccessChain(ptr_type, vs_pica_data_id, ConstU32(2u), ConstU32(index)));
    }

    [[nodiscard]] Id GetFloatUniformDynamic(u32 base_index, u32 addr_component) {
        const Id addr_vec{OpLoad(ivec2_id, addr_reg_id)};
        const Id offset{OpCompositeExtract(i32_id, addr_vec, addr_component - 1)};

        const Id ge{OpSGreaterThanEqual(bool_id, offset, ConstS32(-128))};
        const Id le{OpSLessThanEqual(bool_id, offset, ConstS32(127))};
        const Id in_range{OpLogicalAnd(bool_id, ge, le)};
        const Id fixed_offset{OpSelect(i32_id, in_range, offset, ConstS32(0))};

        const Id raw_index{OpIAdd(i32_id, ConstS32(static_cast<s32>(base_index)), fixed_offset)};
        const Id masked_index{OpBitwiseAnd(i32_id, raw_index, ConstS32(0x7F))};
        const Id in_bounds{OpSLessThan(bool_id, masked_index, ConstS32(96))};
        const Id safe_index{OpSelect(i32_id, in_bounds, masked_index, ConstS32(0))};

        const Id ptr_type{TypePointer(spv::StorageClass::Uniform, vec4_id)};
        const Id loaded{
            OpLoad(vec4_id, OpAccessChain(ptr_type, vs_pica_data_id, ConstU32(2u), safe_index))};
        return OpSelect(vec4_id, in_bounds, loaded, ConstF32(1.f, 1.f, 1.f, 1.f));
    }

    [[nodiscard]] Id LoadVsDataBool(u32 member_index) {
        const Id ptr_type{TypePointer(spv::StorageClass::Uniform, u32_id)};
        const Id loaded{OpLoad(u32_id, OpAccessChain(ptr_type, vs_data_id, ConstU32(member_index)))};
        return OpINotEqual(bool_id, loaded, ConstU32(0u));
    }

    [[nodiscard]] Id LoadClipCoef() {
        const Id ptr_type{TypePointer(spv::StorageClass::Uniform, vec4_id)};
        return OpLoad(vec4_id, OpAccessChain(ptr_type, vs_data_id, ConstU32(2u)));
    }

    [[nodiscard]] Id LoadOperand(const SourceRegister& reg, u32 addr_idx) {
        const u32 index = static_cast<u32>(reg.GetIndex());
        switch (reg.GetRegisterType()) {
        case RegisterType::Input:
            return input_val[index];
        case RegisterType::Temporary:
            return OpLoad(vec4_id, reg_ids[index]);
        case RegisterType::FloatUniform:
            return addr_idx == 0 ? GetFloatUniformConst(index)
                                 : GetFloatUniformDynamic(index, addr_idx);
        default:
            UNREACHABLE();
        }
    }

    [[nodiscard]] Id ApplySwizzleSrc1(Id vec, const SwizzlePattern& swizzle) {
        const Id shuffled{OpVectorShuffle(vec4_id, vec, vec,
                                          static_cast<u32>(swizzle.GetSelectorSrc1(0)),
                                          static_cast<u32>(swizzle.GetSelectorSrc1(1)),
                                          static_cast<u32>(swizzle.GetSelectorSrc1(2)),
                                          static_cast<u32>(swizzle.GetSelectorSrc1(3)))};
        return swizzle.negate_src1 ? OpFNegate(vec4_id, shuffled) : shuffled;
    }

    [[nodiscard]] Id ApplySwizzleSrc2(Id vec, const SwizzlePattern& swizzle) {
        const Id shuffled{OpVectorShuffle(vec4_id, vec, vec,
                                          static_cast<u32>(swizzle.GetSelectorSrc2(0)),
                                          static_cast<u32>(swizzle.GetSelectorSrc2(1)),
                                          static_cast<u32>(swizzle.GetSelectorSrc2(2)),
                                          static_cast<u32>(swizzle.GetSelectorSrc2(3)))};
        return swizzle.negate_src2 ? OpFNegate(vec4_id, shuffled) : shuffled;
    }

    [[nodiscard]] Id ApplySwizzleSrc3(Id vec, const SwizzlePattern& swizzle) {
        const Id shuffled{OpVectorShuffle(vec4_id, vec, vec,
                                          static_cast<u32>(swizzle.GetSelectorSrc3(0)),
                                          static_cast<u32>(swizzle.GetSelectorSrc3(1)),
                                          static_cast<u32>(swizzle.GetSelectorSrc3(2)),
                                          static_cast<u32>(swizzle.GetSelectorSrc3(3)))};
        return swizzle.negate_src3 ? OpFNegate(vec4_id, shuffled) : shuffled;
    }

    [[nodiscard]] std::optional<Id> ResolveDestPointer(const DestRegister& dest_reg) {
        const u32 index = static_cast<u32>(dest_reg.GetIndex());
        if (dest_reg.GetRegisterType() == RegisterType::Output) {
            if (config.state.output_map[index] >= config.state.num_outputs) {
                return std::nullopt;
            }
            return out_reg_ids[config.state.output_map[index]];
        }
        return reg_ids[index];
    }

    void WriteMaskedTo(Id target, const SwizzlePattern& swizzle, Id value) {
        const Id old{OpLoad(vec4_id, target)};
        const Id merged{OpVectorShuffle(vec4_id, old, value,
                                        swizzle.DestComponentEnabled(0) ? 4u : 0u,
                                        swizzle.DestComponentEnabled(1) ? 5u : 1u,
                                        swizzle.DestComponentEnabled(2) ? 6u : 2u,
                                        swizzle.DestComponentEnabled(3) ? 7u : 3u)};
        OpStore(target, merged);
    }

    void CompileMova(Id src1, const SwizzlePattern& swizzle) {
        const Id src1_xy{OpVectorShuffle(vec2_id, src1, src1, 0u, 1u)};
        const Id new_xy{OpConvertFToS(ivec2_id, src1_xy)};
        const Id old{OpLoad(ivec2_id, addr_reg_id)};
        const Id merged{OpVectorShuffle(ivec2_id, old, new_xy,
                                        swizzle.DestComponentEnabled(0) ? 2u : 0u,
                                        swizzle.DestComponentEnabled(1) ? 3u : 1u)};
        OpStore(addr_reg_id, merged);
    }

    [[nodiscard]] Id BroadcastScalar(Id scalar) {
        return OpCompositeConstruct(vec4_id, scalar, scalar, scalar, scalar);
    }

    [[nodiscard]] Id BoolToFloat(Id bvec) {
        return OpSelect(vec4_id, bvec, ConstF32(1.f, 1.f, 1.f, 1.f), ConstF32(0.f, 0.f, 0.f, 0.f));
    }

    [[nodiscard]] Id SanitizeMul4(Id lhs, Id rhs) {
        const Id product{OpFMul(vec4_id, lhs, rhs)};
        const Id nan_lhs{OpIsNan(bvec4_id, lhs)};
        const Id nan_rhs{OpIsNan(bvec4_id, rhs)};
        const Id nan_product{OpIsNan(bvec4_id, product)};
        const Id zero{ConstF32(0.f, 0.f, 0.f, 0.f)};
        const Id inner_rhs{OpSelect(vec4_id, nan_rhs, product, zero)};
        const Id inner{OpSelect(vec4_id, nan_lhs, product, inner_rhs)};
        return OpSelect(vec4_id, nan_product, inner, product);
    }

    [[nodiscard]] Id SanitizeMul3(Id lhs, Id rhs) {
        const Id bvec3_id{TypeVector(bool_id, 3)};
        const Id product{OpFMul(vec3_id, lhs, rhs)};
        const Id nan_lhs{OpIsNan(bvec3_id, lhs)};
        const Id nan_rhs{OpIsNan(bvec3_id, rhs)};
        const Id nan_product{OpIsNan(bvec3_id, product)};
        const Id zero{ConstF32(0.f, 0.f, 0.f)};
        const Id inner_rhs{OpSelect(vec3_id, nan_rhs, product, zero)};
        const Id inner{OpSelect(vec3_id, nan_lhs, product, inner_rhs)};
        return OpSelect(vec3_id, nan_product, inner, product);
    }

    [[nodiscard]] Id DotOp(Id a, Id b, u32 width) {
        if (width == 3) {
            const Id a3{OpVectorShuffle(vec3_id, a, a, 0u, 1u, 2u)};
            const Id b3{OpVectorShuffle(vec3_id, b, b, 0u, 1u, 2u)};
            if (extra.sanitize_mul) {
                return OpDot(f32_id, SanitizeMul3(a3, b3), ConstF32(1.f, 1.f, 1.f));
            }
            return OpDot(f32_id, a3, b3);
        }
        if (extra.sanitize_mul) {
            return OpDot(f32_id, SanitizeMul4(a, b), ConstF32(1.f, 1.f, 1.f, 1.f));
        }
        return OpDot(f32_id, a, b);
    }

    [[nodiscard]] Id CompileArithmeticOp(OpCode::Id op, Id src1, Id src2) {
        switch (op) {
        case OpCode::Id::ADD:
            return OpFAdd(vec4_id, src1, src2);
        case OpCode::Id::MUL:
            return extra.sanitize_mul ? SanitizeMul4(src1, src2) : OpFMul(vec4_id, src1, src2);
        case OpCode::Id::FLR:
            return OpFloor(vec4_id, src1);
        case OpCode::Id::MAX:
            return extra.sanitize_mul
                      ? OpSelect(vec4_id, OpFOrdGreaterThan(bvec4_id, src1, src2), src1, src2)
                      : OpFMax(vec4_id, src1, src2);
        case OpCode::Id::MIN:
            return extra.sanitize_mul
                      ? OpSelect(vec4_id, OpFOrdLessThan(bvec4_id, src1, src2), src1, src2)
                      : OpFMin(vec4_id, src1, src2);
        case OpCode::Id::DP3:
            return BroadcastScalar(DotOp(src1, src2, 3));
        case OpCode::Id::DP4:
            return BroadcastScalar(DotOp(src1, src2, 4));
        case OpCode::Id::DPH:
        case OpCode::Id::DPHI: {
            const Id src1_for_dot =
                extra.sanitize_mul ? OpCompositeInsert(vec4_id, ConstF32(1.f), src1, 3) : src1;
            return BroadcastScalar(DotOp(src1_for_dot, src2, 4));
        }
        case OpCode::Id::MOV:
            return src1;
        case OpCode::Id::SGE:
        case OpCode::Id::SGEI:
            return BoolToFloat(OpFOrdGreaterThanEqual(bvec4_id, src1, src2));
        case OpCode::Id::SLT:
        case OpCode::Id::SLTI:
            return BoolToFloat(OpFOrdLessThan(bvec4_id, src1, src2));
        case OpCode::Id::EX2:
            return BroadcastScalar(OpExp2(f32_id, OpCompositeExtract(f32_id, src1, 0u)));
        case OpCode::Id::LG2:
            return BroadcastScalar(OpLog2(f32_id, OpCompositeExtract(f32_id, src1, 0u)));
        default:
            UNREACHABLE();
        }
    }

    void CompileArithmetic(const Instruction& instr, const OpCode::Info& info) {
        const bool is_inverted = (info.subtype & OpCode::Info::SrcInversed) != 0;
        const OpCode::Id op = instr.opcode.Value().EffectiveOpCode();
        if (op == OpCode::Id::CMP) {
            // Writes only conditional_code, which is dead in a branchless program: nothing ever
            // reads it back (that would require IFC/JMPC, excluded by CanGenerateVertexShader).
            return;
        }

        const SwizzlePattern swizzle{setup.GetSwizzleData()[instr.common.operand_desc_id]};
        const u32 src1_addr = !is_inverted ? instr.common.address_register_index.Value() : 0;
        const u32 src2_addr = is_inverted ? instr.common.address_register_index.Value() : 0;

        const Id src1 =
            ApplySwizzleSrc1(LoadOperand(instr.common.GetSrc1(is_inverted), src1_addr), swizzle);
        const Id src2 =
            ApplySwizzleSrc2(LoadOperand(instr.common.GetSrc2(is_inverted), src2_addr), swizzle);

        if (op == OpCode::Id::MOVA) {
            CompileMova(src1, swizzle);
            return;
        }

        const auto dest_ptr = ResolveDestPointer(instr.common.dest.Value());
        if (!dest_ptr) {
            return;
        }

        Id value;
        if (op == OpCode::Id::RCP || op == OpCode::Id::RSQ) {
            const Id x{OpCompositeExtract(f32_id, src1, 0u)};
            const Id result = op == OpCode::Id::RCP ? OpFDiv(f32_id, ConstF32(1.f), x)
                                                    : OpInverseSqrt(f32_id, x);
            value = BroadcastScalar(result);
            if (!extra.sanitize_mul) {
                const Id cond = op == OpCode::Id::RCP
                                   ? OpFOrdNotEqual(bool_id, x, ConstF32(0.f))
                                   : OpFOrdGreaterThan(bool_id, x, ConstF32(0.f));
                const Id old{OpLoad(vec4_id, *dest_ptr)};
                value = OpSelect(vec4_id, cond, value, old);
            }
        } else {
            value = CompileArithmeticOp(op, src1, src2);
        }

        WriteMaskedTo(*dest_ptr, swizzle, value);
    }

    void CompileMad(const Instruction& instr) {
        const bool is_inverted = instr.opcode.Value().EffectiveOpCode() == OpCode::Id::MADI;
        const SwizzlePattern swizzle{setup.GetSwizzleData()[instr.mad.operand_desc_id]};

        const Id src1 = ApplySwizzleSrc1(LoadOperand(instr.mad.GetSrc1(is_inverted), 0), swizzle);
        const u32 src2_addr = !is_inverted ? instr.mad.address_register_index.Value() : 0;
        const u32 src3_addr = is_inverted ? instr.mad.address_register_index.Value() : 0;
        const Id src2 =
            ApplySwizzleSrc2(LoadOperand(instr.mad.GetSrc2(is_inverted), src2_addr), swizzle);
        const Id src3 =
            ApplySwizzleSrc3(LoadOperand(instr.mad.GetSrc3(is_inverted), src3_addr), swizzle);

        const auto dest_ptr = ResolveDestPointer(instr.mad.dest.Value());
        if (!dest_ptr) {
            return;
        }

        const Id product =
            extra.sanitize_mul ? SanitizeMul4(src1, src2) : OpFMul(vec4_id, src1, src2);
        const Id value{OpFAdd(vec4_id, product, src3)};
        WriteMaskedTo(*dest_ptr, swizzle, value);
    }

    [[nodiscard]] Id SanitizeVertex(Id vtx_pos) {
        const Id z{OpCompositeExtract(f32_id, vtx_pos, 2u)};
        const Id w{OpCompositeExtract(f32_id, vtx_pos, 3u)};
        const Id ndc_z{OpFDiv(f32_id, z, w)};

        const Id cond1{OpLogicalAnd(bool_id, OpFOrdGreaterThan(bool_id, ndc_z, ConstF32(0.f)),
                                    OpFOrdLessThan(bool_id, ndc_z, ConstF32(0.000001f)))};
        const Id z1{OpSelect(f32_id, cond1, ConstF32(0.f), z)};

        const Id cond2{OpLogicalAnd(bool_id, OpFOrdLessThan(bool_id, ndc_z, ConstF32(-1.f)),
                                    OpFOrdGreaterThan(bool_id, ndc_z, ConstF32(-1.00001f)))};
        const Id neg_w{OpFNegate(f32_id, w)};
        const Id z2{OpSelect(f32_id, cond2, neg_w, z1)};

        return OpCompositeInsert(vec4_id, z2, vtx_pos, 2u);
    }

    void WriteOutputs() {
        const auto semantic_maps = config.state.gs_state.GetSemanticMaps();
        const auto semantic = [&](VSOutputAttributes::Semantic slot) -> Id {
            const u32 s = static_cast<u32>(slot);
            const u32 attrib = semantic_maps[s].attribute_index;
            const u32 comp = semantic_maps[s].component_index;
            if (attrib < config.state.gs_state.gs_output_attributes_count) {
                const Id v{OpLoad(vec4_id, out_reg_ids[attrib])};
                return OpCompositeExtract(f32_id, v, comp);
            }
            return ConstF32(1.f);
        };

        Id vtx_pos = OpCompositeConstruct(
            vec4_id, semantic(VSOutputAttributes::POSITION_X), semantic(VSOutputAttributes::POSITION_Y),
            semantic(VSOutputAttributes::POSITION_Z), semantic(VSOutputAttributes::POSITION_W));
        vtx_pos = SanitizeVertex(vtx_pos);

        const Id flip_viewport{LoadVsDataBool(1)};
        const Id y{OpCompositeExtract(f32_id, vtx_pos, 1u)};
        const Id neg_y{OpFNegate(f32_id, y)};
        const Id y_final{OpSelect(f32_id, flip_viewport, neg_y, y)};
        vtx_pos = OpCompositeInsert(vec4_id, y_final, vtx_pos, 1u);

        const Id x{OpCompositeExtract(f32_id, vtx_pos, 0u)};
        const Id z{OpCompositeExtract(f32_id, vtx_pos, 2u)};
        const Id w{OpCompositeExtract(f32_id, vtx_pos, 3u)};
        const Id gl_pos{OpCompositeConstruct(vec4_id, x, y_final, OpFNegate(f32_id, z), w)};
        OpStore(gl_position_id, gl_pos);

        if (extra.use_clip_planes) {
            const Id clip_ptr_type{TypePointer(spv::StorageClass::Output, f32_id)};
            OpStore(OpAccessChain(clip_ptr_type, gl_clip_distance_id, ConstU32(0u)),
                   OpFNegate(f32_id, z));

            const Id enable_clip1{LoadVsDataBool(0)};
            const Id clip_coef{LoadClipCoef()};
            const Id dot_val{OpDot(f32_id, clip_coef, vtx_pos)};
            const Id clip1{OpSelect(f32_id, enable_clip1, dot_val, ConstF32(0.f))};
            OpStore(OpAccessChain(clip_ptr_type, gl_clip_distance_id, ConstU32(1u)), clip1);
        }

        const Id normquat{OpCompositeConstruct(
            vec4_id, semantic(VSOutputAttributes::QUATERNION_X),
            semantic(VSOutputAttributes::QUATERNION_Y), semantic(VSOutputAttributes::QUATERNION_Z),
            semantic(VSOutputAttributes::QUATERNION_W))};
        OpStore(normquat_id, normquat);

        const Id vtx_color{OpCompositeConstruct(
            vec4_id, semantic(VSOutputAttributes::COLOR_R), semantic(VSOutputAttributes::COLOR_G),
            semantic(VSOutputAttributes::COLOR_B), semantic(VSOutputAttributes::COLOR_A))};
        const Id primary_color{
            OpFMin(vec4_id, OpFAbs(vec4_id, vtx_color), ConstF32(1.f, 1.f, 1.f, 1.f))};
        OpStore(primary_color_id, primary_color);

        OpStore(texcoord0_id, OpCompositeConstruct(vec2_id,
                                                    semantic(VSOutputAttributes::TEXCOORD0_U),
                                                    semantic(VSOutputAttributes::TEXCOORD0_V)));
        OpStore(texcoord1_id, OpCompositeConstruct(vec2_id,
                                                    semantic(VSOutputAttributes::TEXCOORD1_U),
                                                    semantic(VSOutputAttributes::TEXCOORD1_V)));
        OpStore(texcoord0_w_id, semantic(VSOutputAttributes::TEXCOORD0_W));
        OpStore(view_id, OpCompositeConstruct(vec3_id, semantic(VSOutputAttributes::VIEW_X),
                                              semantic(VSOutputAttributes::VIEW_Y),
                                              semantic(VSOutputAttributes::VIEW_Z)));
        OpStore(texcoord2_id, OpCompositeConstruct(vec2_id,
                                                    semantic(VSOutputAttributes::TEXCOORD2_U),
                                                    semantic(VSOutputAttributes::TEXCOORD2_V)));
    }

private:
    const Pica::ShaderSetup& setup;
    const PicaVSConfig& config;
    const ExtraVSConfig& extra;
    std::array<bool, 16> used_regs{};

    Id void_id{};
    Id bool_id{};
    Id f32_id{};
    Id i32_id{};
    Id u32_id{};
    Id vec2_id{};
    Id vec3_id{};
    Id vec4_id{};
    Id ivec2_id{};
    Id ivec4_id{};
    Id uvec4_id{};
    Id bvec4_id{};

    Id vs_pica_data_id{};
    Id vs_data_id{};

    Id primary_color_id{};
    Id texcoord0_id{};
    Id texcoord1_id{};
    Id texcoord2_id{};
    Id texcoord0_w_id{};
    Id normquat_id{};
    Id view_id{};
    Id gl_position_id{};
    Id gl_clip_distance_id{};

    std::array<Id, 16> input_var_ids{};
    std::array<Id, 16> input_val{};
    std::array<Id, 16> reg_ids{};
    std::array<Id, 16> out_reg_ids{};
    Id addr_reg_id{};
};

} // namespace

std::vector<u32> GenerateVertexShader(const Pica::ShaderSetup& setup, const PicaVSConfig& config,
                                      const ExtraVSConfig& extra) {
    VertexModule module{setup, config, extra};
    module.Generate();
    return module.Assemble();
}

} // namespace Pica::Shader::Generator::SPIRV
