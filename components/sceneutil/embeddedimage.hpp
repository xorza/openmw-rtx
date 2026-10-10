#pragma once

#include <string>
#include <string_view>
#include <typeinfo>
#include <utility>

#include <osg/CopyOp>
#include <osg/Image>
#include <osg/Object>
#include <osg/Referenced>
#include <osg/UserDataContainer>

namespace SceneUtil
{
    /// What an image a model carries inside it is known by, where it has no file of its own: the
    /// model's path and the record that holds its texels. Two references of one model then share
    /// one image's texture, and two models never do. Vanilla has one such model,
    /// `meshes\i\tx_crystal_02.nif`.
    ///
    /// **A stamp and not a file name**, so nothing that loads images by name tries to open the key.
    /// Kept in the image's user data slot, as `StableIdentity` is kept on a node, where the loader
    /// writes nothing else.
    class EmbeddedImage final : public osg::Object
    {
    public:
        EmbeddedImage() = default;

        explicit EmbeddedImage(std::string key)
            : mKey(std::move(key))
        {
        }

        EmbeddedImage(const EmbeddedImage& copy, const osg::CopyOp& copyop = osg::CopyOp::SHALLOW_COPY)
            : osg::Object(copy, copyop)
            , mKey(copy.mKey)
        {
        }

        META_Object(SceneUtil, EmbeddedImage)

        /// The model's path, a `#` and the record's index.
        std::string_view getKey() const { return mKey; }

        /// Stamps `image`, which `model` carries as its record `record`.
        static void stamp(osg::Image& image, std::string_view model, unsigned int record)
        {
            image.setUserData(new EmbeddedImage(std::string(model) + "#" + std::to_string(record)));
        }

        /// What names `image` to whatever keys a texture by name: its file, or the stamp of an image
        /// that has none, or nothing.
        static std::string_view nameOf(const osg::Image& image)
        {
            if (!image.getFileName().empty())
                return image.getFileName();
            if (const EmbeddedImage* stamped = find(image))
                return stamped->getKey();
            return {};
        }

        /// The stamp `image` carries, or null where it carries none.
        static const EmbeddedImage* find(const osg::Image& image)
        {
            const osg::UserDataContainer* held = image.getUserDataContainer();
            if (held == nullptr)
                return nullptr;

            // An exact type test, as `StableIdentity::find` makes for the reason it gives.
            const osg::Referenced* data = held->getUserData();
            if (data == nullptr || typeid(*data) != typeid(EmbeddedImage))
                return nullptr;

            return static_cast<const EmbeddedImage*>(data);
        }

    private:
        std::string mKey;
    };
}
