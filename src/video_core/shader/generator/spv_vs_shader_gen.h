// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <vector>
#include "common/common_types.h"
#include "video_core/pica/shader_setup.h"

namespace Pica {
struct ShaderSetup;
}

namespace Pica::Shader::Generator {
struct PicaVSConfig;
struct ExtraVSConfig;
} // namespace Pica::Shader::Generator

namespace Pica::Shader::Generator::SPIRV {

/// Returns true if the PICA vertex program starting at main_offset uses no control-flow
/// instructions and only arithmetic instructions VertexModule knows how to emit directly to
/// SPIR-V. Anything else (branches, loops, subroutine calls, or unsupported opcodes like DST/LIT)
/// must go through the GLSL/glslang path instead.
[[nodiscard]] bool CanGenerateVertexShader(const Pica::ProgramCode& program_code, u32 main_offset);

/**
 * Generates the SPIR-V vertex shader program for a branchless PICA vertex program.
 * @param setup ShaderSetup object holding the PICA program code, swizzle data and uniforms
 * @param config ShaderCacheKey object generated for the current Pica state
 * @param extra Complementary user/driver information used to generate the vertex shader
 * @returns SPIR-V bytecode, or an empty vector if the shader could not be generated
 */
[[nodiscard]] std::vector<u32> GenerateVertexShader(const Pica::ShaderSetup& setup,
                                                     const PicaVSConfig& config,
                                                     const ExtraVSConfig& extra);

} // namespace Pica::Shader::Generator::SPIRV
