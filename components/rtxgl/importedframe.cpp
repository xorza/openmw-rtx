#include "importedframe.hpp"

#include <format>
#include <stdexcept>
#include <vector>

#include <SDL_video.h>

#include <GL/gl.h>
#include <GL/glext.h>

namespace RtxGl
{
    namespace
    {
        /// The entry points this needs, loaded once from whichever context was current first.
        ///
        /// **Through SDL and not through OSG's `GLExtensions`.** That one is per graphics context
        /// and is reached from inside a draw callback; the import happens once, at setup, from
        /// whoever owns the frame — and the harness that proves this works has no OSG viewer at all.
        struct Entries
        {
            PFNGLGETUNSIGNEDBYTEI_VEXTPROC mGetUnsignedBytei = nullptr;
            PFNGLCREATEMEMORYOBJECTSEXTPROC mCreateMemoryObjects = nullptr;
            PFNGLDELETEMEMORYOBJECTSEXTPROC mDeleteMemoryObjects = nullptr;
            PFNGLIMPORTMEMORYFDEXTPROC mImportMemoryFd = nullptr;
            PFNGLTEXTURESTORAGEMEM2DEXTPROC mTextureStorageMem2D = nullptr;
            PFNGLCREATETEXTURESPROC mCreateTextures = nullptr;
            PFNGLGETTEXTUREIMAGEPROC mGetTextureImage = nullptr;
            PFNGLTEXTUREPARAMETERIPROC mTextureParameteri = nullptr;
            PFNGLCREATEFRAMEBUFFERSPROC mCreateFramebuffers = nullptr;
            PFNGLDELETEFRAMEBUFFERSPROC mDeleteFramebuffers = nullptr;
            PFNGLNAMEDFRAMEBUFFERTEXTUREPROC mNamedFramebufferTexture = nullptr;
            PFNGLBLITNAMEDFRAMEBUFFERPROC mBlitNamedFramebuffer = nullptr;

            /// What is missing, or empty where nothing is.
            std::string mObstacle;
        };

        const Entries& entries()
        {
            static const Entries sLoaded = [] {
                Entries found;
                const auto load = [&](auto& slot, const char* name) {
                    slot = reinterpret_cast<std::remove_reference_t<decltype(slot)>>(SDL_GL_GetProcAddress(name));
                    if (slot == nullptr && found.mObstacle.empty())
                        found.mObstacle = std::string("this OpenGL context has no ") + name;
                };

                load(found.mGetUnsignedBytei, "glGetUnsignedBytei_vEXT");
                load(found.mCreateMemoryObjects, "glCreateMemoryObjectsEXT");
                load(found.mDeleteMemoryObjects, "glDeleteMemoryObjectsEXT");
                load(found.mImportMemoryFd, "glImportMemoryFdEXT");
                load(found.mTextureStorageMem2D, "glTextureStorageMem2DEXT");
                load(found.mCreateTextures, "glCreateTextures");
                load(found.mGetTextureImage, "glGetTextureImage");
                load(found.mTextureParameteri, "glTextureParameteri");
                load(found.mCreateFramebuffers, "glCreateFramebuffers");
                load(found.mDeleteFramebuffers, "glDeleteFramebuffers");
                load(found.mNamedFramebufferTexture, "glNamedFramebufferTexture");
                load(found.mBlitNamedFramebuffer, "glBlitNamedFramebuffer");

                return found;
            }();

            return sLoaded;
        }
    }

    std::string describeGlDevice()
    {
        const Entries& gl = entries();
        if (!gl.mObstacle.empty())
            return {};

        DeviceUuid uuid{};
        gl.mGetUnsignedBytei(GL_DEVICE_UUID_EXT, 0, uuid.data());

        std::string text;
        for (const std::uint8_t byte : uuid)
            text += std::format("{:02x}", byte);

        return text;
    }

    std::string findDeviceMismatch(const DeviceUuid& vulkan)
    {
        const Entries& gl = entries();
        if (!gl.mObstacle.empty())
            return gl.mObstacle;

        DeviceUuid mine{};
        gl.mGetUnsignedBytei(GL_DEVICE_UUID_EXT, 0, mine.data());

        if (mine == vulkan)
            return {};

        std::string theirs;
        std::string ours;
        for (std::size_t at = 0; at < mine.size(); ++at)
        {
            ours += std::format("{:02x}", mine[at]);
            theirs += std::format("{:02x}", vulkan[at]);
        }

        // **Named rather than worked around.** An import across two devices does not reliably fail;
        // it succeeds and the texture holds whatever was at that address. Anyone who ever sees this
        // has a machine where the window went to a different GPU than the tracer.
        return "OpenGL is on device " + ours + " and the ray tracer is on " + theirs
            + ", so nothing one renders can be imported by the other";
    }

    ImportedFrame::ImportedFrame(int memory, std::uint64_t bytes, std::uint32_t width, std::uint32_t height)
        : mWidth(width)
        , mHeight(height)
    {
        const Entries& gl = entries();
        if (!gl.mObstacle.empty())
            throw std::runtime_error(gl.mObstacle);

        gl.mCreateMemoryObjects(1, &mMemory);

        // **GL closes the descriptor**, on success and on failure alike — which is the opposite of
        // the Vulkan import beside it, and the reason this takes ownership rather than borrowing.
        gl.mImportMemoryFd(mMemory, bytes, GL_HANDLE_TYPE_OPAQUE_FD_EXT, memory);

        gl.mCreateTextures(GL_TEXTURE_2D, 1, &mTexture);

        // Optimal and not linear, because that is how Vulkan created the image. The two sides have
        // to agree on the tiling or they agree on the bytes and not on their order.
        gl.mTextureParameteri(mTexture, GL_TEXTURE_TILING_EXT, GL_OPTIMAL_TILING_EXT);
        gl.mTextureStorageMem2D(
            mTexture, 1, GL_RGBA8, static_cast<GLsizei>(width), static_cast<GLsizei>(height), mMemory, 0);

        gl.mCreateFramebuffers(1, &mReadFrom);
        gl.mNamedFramebufferTexture(mReadFrom, GL_COLOR_ATTACHMENT0, mTexture, 0);

        if (const GLenum failed = glGetError(); failed != GL_NO_ERROR)
            throw std::runtime_error(std::format("OpenGL would not import the frame: error {:#x}", failed));
    }

    void ImportedFrame::blit(std::uint32_t width, std::uint32_t height) const
    {
        const Entries& gl = entries();

        // Whatever is bound for drawing, which is the one OSG is composing into rather than the
        // window's own — the frame goes under the GUI, not over it.
        GLint target = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &target);

        // **Flipped, and it is not a matter of taste.** The tracer writes row zero at the top of
        // the picture, the way a screenshot is laid out and the way `readPixels` hands it back;
        // OpenGL's framebuffer origin is the bottom left. Blitting straight across puts the sky on
        // the floor. Swapping the destination's Y rather than the source's keeps the source
        // rectangle the whole texture, which is what `GL_LINEAR` wants when the sizes differ.
        gl.mBlitNamedFramebuffer(mReadFrom, static_cast<GLuint>(target), 0, 0, static_cast<GLint>(mWidth),
            static_cast<GLint>(mHeight), 0, static_cast<GLint>(height), static_cast<GLint>(width), 0,
            GL_COLOR_BUFFER_BIT, GL_LINEAR);
    }

    ImportedFrame::~ImportedFrame()
    {
        if (mReadFrom != 0)
            entries().mDeleteFramebuffers(1, &mReadFrom);

        if (mTexture != 0)
            glDeleteTextures(1, &mTexture);

        if (mMemory != 0)
            entries().mDeleteMemoryObjects(1, &mMemory);
    }

    void ImportedFrame::read(std::vector<std::uint8_t>& pixels) const
    {
        pixels.resize(std::size_t{ mWidth } * mHeight * 4);
        entries().mGetTextureImage(
            mTexture, 0, GL_RGBA, GL_UNSIGNED_BYTE, static_cast<GLsizei>(pixels.size()), pixels.data());
    }
}
