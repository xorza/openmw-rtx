#pragma once

#include <cstdint>

#include <osg/StateAttribute>

namespace SceneUtil
{
    // The type bound to the given texture used by the ShaderVisitor to distinguish between them
    class TextureType : public osg::StateAttribute
    {
    public:
        TextureType() = default;

        explicit TextureType(const std::string& name) { setName(name); }

        TextureType(const TextureType& copy, const osg::CopyOp& copyop = osg::CopyOp::SHALLOW_COPY)
            : StateAttribute(copy, copyop)
        {
        }
        ~TextureType() override = default;
        TextureType& operator=(const TextureType&) = delete;

        static const osg::StateAttribute::Type AttributeType = static_cast<osg::StateAttribute::Type>(69);
        META_StateAttribute(SceneUtil, TextureType, AttributeType)

        bool isTextureAttribute() const override { return true; }

        std::int32_t compare(const osg::StateAttribute& sa) const override
        {
            // NOLINTNEXTLINE(modernize-use-auto): OSG's macro declares the type it is handed, which `auto` cannot be.
            COMPARE_StateAttribute_Types(TextureType, sa);
            COMPARE_StateAttribute_Parameter(_name);
            return 0;
        }
    };
}
