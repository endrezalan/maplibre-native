#pragma once

#include <mbgl/gfx/shader_group.hpp>
#include <mbgl/shaders/mtl/shader_program.hpp>
#include <mbgl/shaders/shader_source.hpp>
#include <mbgl/programs/program_parameters.hpp>

#include <numeric>
#include <string>
#include <unordered_map>

namespace mbgl {
namespace mtl {

template <shaders::BuiltIn ShaderID>
class ShaderGroup final : public gfx::ShaderGroup {
public:
    ShaderGroup(const ProgramParameters& programParameters_, std::vector<std::string> bufferNames_)
        : gfx::ShaderGroup(),
          programParameters(programParameters_),
          bufferNames(std::move(bufferNames_)) {}
    ~ShaderGroup() noexcept override = default;

    gfx::ShaderPtr getOrCreateShader(gfx::Context& gfxContext,
                                     const std::vector<std::string>& propertiesAsUniforms,
                                     std::string_view /*firstAttribName*/) override {
        constexpr auto& name = shaders::ShaderSource<ShaderID, gfx::Backend::Type::Metal>::name;
        constexpr auto& source = shaders::ShaderSource<ShaderID, gfx::Backend::Type::Metal>::source;
        constexpr auto& vertMain = shaders::ShaderSource<ShaderID, gfx::Backend::Type::Metal>::vertexMainFunction;
        constexpr auto& fragMain = shaders::ShaderSource<ShaderID, gfx::Backend::Type::Metal>::fragmentMainFunction;

        uint32_t key = 0;
        std::unordered_map<std::string, std::string> additionalDefines;
        for (unsigned int i = 0; i < propertiesAsUniforms.size(); i++) {
            if (!propertiesAsUniforms[i].empty()) {
                key |= 1 << i;
                auto name = std::string("HAS_UNIFORM_u_") + propertiesAsUniforms[i];
                additionalDefines.insert(std::make_pair(std::move(name), "1"));
            }
        }

        const std::string shaderName = std::string(name) + "#" + std::to_string(key);
        auto shader = get<mtl::ShaderProgram>(shaderName);
        if (!shader) {
            auto& context = static_cast<Context&>(gfxContext);
            shader = context.createProgram(
                shaderName, source, vertMain, fragMain, programParameters, additionalDefines);
            assert(shader);
            if (!shader || !registerShader(shader, shaderName)) {
                assert(false);
                throw std::runtime_error("Failed to register " + shaderName + " with shader group!");
            }
            shader->setBufferNames(bufferNames);
        }
        return shader;
    }

private:
    ProgramParameters programParameters;
    std::vector<std::string> bufferNames;
};

} // namespace mtl
} // namespace mbgl
