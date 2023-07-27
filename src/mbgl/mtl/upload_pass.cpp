#include <mbgl/mtl/upload_pass.hpp>

#include <mbgl/mtl/context.hpp>
#include <mbgl/mtl/command_encoder.hpp>
#include <mbgl/mtl/index_buffer_resource.hpp>
#include <mbgl/mtl/vertex_attribute.hpp>
#include <mbgl/mtl/vertex_buffer_resource.hpp>
#include <mbgl/util/logging.hpp>

#include <algorithm>

namespace mbgl {
namespace mtl {

UploadPass::UploadPass(CommandEncoder& commandEncoder_, const char* name)
    : commandEncoder(commandEncoder_) {}

std::unique_ptr<gfx::VertexBufferResource> UploadPass::createVertexBufferResource(const void* data,
                                                                                  const std::size_t size,
                                                                                  const gfx::BufferUsageType usage) {
    return std::make_unique<VertexBufferResource>(commandEncoder.context.createBuffer(data, size, usage));
}

void UploadPass::updateVertexBufferResource(gfx::VertexBufferResource& resource, const void* data, std::size_t size) {
    static_cast<VertexBufferResource&>(resource).get().update(data, size, /*offset=*/0);
}

std::unique_ptr<gfx::IndexBufferResource> UploadPass::createIndexBufferResource(const void* data,
                                                                                const std::size_t size,
                                                                                const gfx::BufferUsageType usage) {
    return std::make_unique<IndexBufferResource>(commandEncoder.context.createBuffer(data, size, usage));
}

void UploadPass::updateIndexBufferResource(gfx::IndexBufferResource& resource, const void* data, std::size_t size) {
    static_cast<IndexBufferResource&>(resource).get().update(data, size, /*offset=*/0);
}

std::unique_ptr<gfx::TextureResource> UploadPass::createTextureResource(const Size size,
                                                                        const void* data,
                                                                        gfx::TexturePixelType format,
                                                                        gfx::TextureChannelDataType type) {
    //    auto obj = commandEncoder.context.createUniqueTexture();
    //    const int textureByteSize = gl::TextureResource::getStorageSize(size, format, type);
    //    commandEncoder.context.renderingStats().memTextures += textureByteSize;
    //    auto resource = std::make_unique<gl::TextureResource>(std::move(obj), textureByteSize);
    //    commandEncoder.context.pixelStoreUnpack = {1};
    //    updateTextureResource(*resource, size, data, format, type);
    //    // We are using clamp to edge here since OpenGL ES doesn't allow GL_REPEAT
    //    // on NPOT textures. We use those when the pixelRatio isn't a power of two,
    //    // e.g. on iPhone 6 Plus.
    //    MBGL_CHECK_ERROR(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
    //    MBGL_CHECK_ERROR(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
    //    MBGL_CHECK_ERROR(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
    //    MBGL_CHECK_ERROR(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
    return std::make_unique<TextureResource>();
}

void UploadPass::updateTextureResource(gfx::TextureResource& resource,
                                       const Size size,
                                       const void* data,
                                       gfx::TexturePixelType format,
                                       gfx::TextureChannelDataType type) {}

void UploadPass::updateTextureResourceSub(gfx::TextureResource& resource,
                                          const uint16_t xOffset,
                                          const uint16_t yOffset,
                                          const Size size,
                                          const void* data,
                                          gfx::TexturePixelType format,
                                          gfx::TextureChannelDataType type) {}

struct VertexBuffer : public gfx::VertexBufferBase {
    ~VertexBuffer() override = default;

    std::unique_ptr<gfx::VertexBufferResource> resource;
};

namespace {
const std::unique_ptr<gfx::VertexBufferResource> noBuffer;
}
const gfx::UniqueVertexBufferResource& UploadPass::getBuffer(const gfx::VertexVectorBasePtr& vec,
                                                             const gfx::BufferUsageType usage) {
    if (vec) {
        const auto* rawBufPtr = vec->getRawData();
        const auto rawBufSize = vec->getRawCount() * vec->getRawSize();

        // If we already have a buffer...
        if (auto* rawData = static_cast<VertexBuffer*>(vec->getBuffer()); rawData && rawData->resource) {
            auto& resource = static_cast<VertexBufferResource&>(*rawData->resource);

            // If it's changed, update it
            if (rawBufSize <= resource.getSizeInBytes()) {
                if (vec->getDirty()) {
                    updateVertexBufferResource(resource, rawBufPtr, rawBufSize);
                }
                return rawData->resource;
            }
        }
        // Otherwise, create a new one
        if (rawBufSize > 0) {
            auto buffer = std::make_unique<VertexBuffer>();
            buffer->resource = createVertexBufferResource(rawBufPtr, rawBufSize, usage);
            vec->setBuffer(std::move(buffer));
            return static_cast<VertexBuffer*>(vec->getBuffer())->resource;
        }
    }
    return noBuffer;
}

// static std::size_t padSize(std::size_t size, std::size_t padding) {
//     return (padding - (size % padding)) % padding;
// }
// template <typename T>
// static std::size_t pad(std::vector<T>& vector, std::size_t size, T value) {
//     const auto count = padSize(vector.size(), size);
//     vector.insert(vector.end(), count, value);
//     return count;
// }

gfx::AttributeBindingArray UploadPass::buildAttributeBindings(
    const std::size_t vertexCount,
    const gfx::AttributeDataType vertexType,
    const std::size_t vertexAttributeIndex,
    const std::vector<std::uint8_t>& vertexData,
    const gfx::VertexAttributeArray& defaults,
    const gfx::VertexAttributeArray& overrides,
    const gfx::BufferUsageType usage,
    /*out*/ std::vector<std::unique_ptr<gfx::VertexBufferResource>>& outBuffers) {
    gfx::AttributeBindingArray bindings;
    bindings.resize(defaults.size());

    constexpr std::size_t align = 16;
    constexpr std::uint8_t padding = 0;

    std::vector<std::uint8_t> allData;
    allData.reserve((defaults.getTotalSize() + align) * vertexCount);

    uint32_t vertexStride = 0;

    // For each attribute in the program, with the corresponding default and optional override...
    const auto resolveAttr = [&](const std::string& name, auto& default_, auto& override_) -> void {
        auto& effectiveAttr = override_ ? *override_ : default_;
        const auto& defaultAttr = static_cast<const VertexAttribute&>(default_);
        const auto stride = defaultAttr.getStride();
        const auto offset = static_cast<uint32_t>(allData.size());
        const auto index = static_cast<std::size_t>(defaultAttr.getIndex());

        bindings.resize(std::max(bindings.size(), index + 1));

        if (const auto& buffer = getBuffer(effectiveAttr.getSharedRawData(), usage)) {
            bindings[index] = {
                /*.attribute = */ {effectiveAttr.getSharedType(), effectiveAttr.getSharedOffset()},
                /*.vertexStride = */ effectiveAttr.getSharedStride(),
                /*.vertexBufferResource = */ buffer.get(),
                /*.vertexOffset = */ effectiveAttr.getSharedVertexOffset(),
            };
            return;
        }

        assert(false);
#if 0
        // Get the raw data for the values in the desired format
        const auto& rawData = VertexAttribute::getRaw(effectiveAttr, defaultAttr.getGLType());
        if (rawData.empty()) {
            VertexAttributeGL::getRaw(effectiveAttr, defaultAttr.getGLType());
        }

        if (rawData.size() == stride * vertexCount) {
            // The override provided a value for each vertex, append it as-is
            allData.insert(allData.end(), rawData.begin(), rawData.end());
        } else if (rawData.size() == stride) {
            // We only have one value, append a copy for each vertex
            for (std::size_t i = 0; i < vertexCount; ++i) {
                allData.insert(allData.end(), rawData.begin(), rawData.end());
            }
        } else {
            // something else, the binding is invalid
            // TODO: throw?
            Log::Warning(Event::General,
                         "Got " + util::toString(rawData.size()) + " bytes for attribute '" + name + "' (" +
                             util::toString(defaultGL.getIndex()) + "), expected " + util::toString(stride) + " or
                             " + util::toString(stride * vertexCount));
            return;
        }

        bindings[index] = {
            /*.attribute = */ {defaultAttr.getDataType(), offset},
            /* vertexStride = */ static_cast<uint32_t>(stride),
            /* vertexBufferResource = */ nullptr, // buffer details established later
            /* vertexOffset = */ 0,
        };

        pad(allData, align, padding);

        // The vertex stride is the sum of the attribute strides
        vertexStride += static_cast<uint32_t>(stride);
#endif
    };
    defaults.resolve(overrides, resolveAttr);

    assert(vertexStride * vertexCount <= allData.size());

    if (!allData.empty()) {
        if (auto vertBuf = createVertexBufferResource(allData.data(), allData.size(), usage)) {
            // Fill in the buffer in each binding that was generated without its own buffer
            std::for_each(bindings.begin(), bindings.end(), [&](auto& b) {
                if (b && !b->vertexBufferResource) {
                    b->vertexBufferResource = vertBuf.get();
                }
            });

            outBuffers.emplace_back(std::move(vertBuf));
        } else {
            assert(false);
            return {};
        }
    }

    assert(std::all_of(bindings.begin(), bindings.end(), [](const auto& b) { return !b || b->vertexBufferResource; }));

    return bindings;
}

void UploadPass::pushDebugGroup(const char* name) {}

void UploadPass::popDebugGroup() {}

gfx::Context& UploadPass::getContext() {
    return commandEncoder.context;
}

const gfx::Context& UploadPass::getContext() const {
    return commandEncoder.context;
}

} // namespace mtl
} // namespace mbgl
