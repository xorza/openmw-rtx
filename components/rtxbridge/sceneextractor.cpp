#include "sceneextractor.hpp"

#include <osg/BlendFunc>
#include <osg/CullFace>
#include <osg/Geometry>
#include <osg/Image>
#include <osg/Material>
#include <osg/NodeVisitor>
#include <osg/Texture2D>
#include <osg/TriangleIndexFunctor>

#include <array>
#include <cassert>

#include <components/sceneutil/texturetype.hpp>
// `terraindrawable.hpp` holds `osg::ref_ptr`s to composite-map types it only forward-declares, so it
// does not compile on its own. This is what completes them.
#include <components/terrain/compositemaprenderer.hpp>
#include <components/terrain/terraindrawable.hpp>

namespace RtxBridge
{
    namespace
    {
        /// Collects triangle indices whatever primitive mode the geometry used.
        ///
        /// Strips, fans and quads all arrive here as triangles, which is the only form an
        /// acceleration structure takes. Degenerate triangles — how a strip restarts — are dropped:
        /// they contribute no surface and a zero-area triangle in a BLAS is wasted traversal.
        struct TriangleCollector
        {
            std::vector<std::uint32_t>* mIndices = nullptr;

            void operator()(unsigned int a, unsigned int b, unsigned int c) const
            {
                if (a == b || b == c || a == c)
                    return;

                mIndices->push_back(a);
                mIndices->push_back(b);
                mIndices->push_back(c);
            }
        };

        /// The state set nearest the drawable, or null when nothing on the path has one.
        ///
        /// This is the material's identity. Two drawables that share it share their shading: OpenMW's
        /// optimizer collapses equivalent state sets into one object, so sharing the pointer means
        /// sharing the values, and what the parents above contribute in this graph is light and
        /// render-bin state rather than material.
        const osg::StateSet* findOwnStateSet(const osg::NodePath& path)
        {
            for (auto it = path.rbegin(); it != path.rend(); ++it)
                if (const osg::StateSet* stateSet = (*it)->getStateSet())
                    return stateSet;

            return nullptr;
        }

        /// The state set nearest the drawable that binds any texture.
        ///
        /// Separate from `findOwnStateSet` because the two can differ: `NifOsg` puts a model's
        /// textures on the geometry, but a drawable can carry a state set of its own that only sets
        /// culling or blending. Taking the whole material from whichever state set happened to hold
        /// the textures would then hand it a parent's two-sidedness.
        const osg::StateSet* findTexturedStateSet(const osg::NodePath& path)
        {
            for (auto it = path.rbegin(); it != path.rend(); ++it)
                if (const osg::StateSet* stateSet = (*it)->getStateSet())
                    if (!stateSet->getTextureAttributeList().empty())
                        return stateSet;

            return nullptr;
        }

        /// The texture bound at `unit`, or null.
        const osg::Texture2D* getTexture(const osg::StateSet& stateSet, unsigned int unit)
        {
            return dynamic_cast<const osg::Texture2D*>(
                stateSet.getTextureAttribute(unit, osg::StateAttribute::TEXTURE));
        }

        /// The role OpenMW's shader visitor assigned to `unit` — "diffuseMap", "normalMap" and the
        /// rest — or empty when it did not run or did not recognise the slot.
        std::string_view getTextureRole(const osg::StateSet& stateSet, unsigned int unit)
        {
            const osg::StateAttribute* attribute
                = stateSet.getTextureAttribute(unit, SceneUtil::TextureType::AttributeType);
            if (attribute == nullptr)
                return {};

            return attribute->getName();
        }

        bool isBlended(const osg::StateSet& stateSet)
        {
            return (stateSet.getMode(GL_BLEND) & osg::StateAttribute::ON) != 0
                && stateSet.getAttribute(osg::StateAttribute::BLENDFUNC) != nullptr;
        }

        /// Whether back faces are drawn.
        ///
        /// OpenGL culls nothing unless told to, and `NifOsg` adds a `CullFace` only where the model's
        /// stencil property asked for it — so the absence of the attribute means two-sided, not
        /// one-sided. Getting this backwards lights every sheet in the game from one side only.
        bool isTwoSided(const osg::StateSet& stateSet)
        {
            const osg::StateAttribute* attribute = stateSet.getAttribute(osg::StateAttribute::CULLFACE);
            if (attribute == nullptr)
                return true;

            return (stateSet.getMode(GL_CULL_FACE) & osg::StateAttribute::ON) == 0;
        }

        /// The image's format, as a name a person can act on.
        std::string describeFormat(const osg::Image& image)
        {
            std::string name;
            switch (image.getPixelFormat())
            {
                // One entry for both spellings: whether the file's header claimed alpha decides
                // nothing, since a BC1 block carries its punch-through bit either way.
                case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
                case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
                    name = "BC1 (DXT1)";
                    break;
                case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
                    name = "BC2 (DXT3)";
                    break;
                case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
                    name = "BC3 (DXT5)";
                    break;
                case GL_RGB:
                    name = "RGB8";
                    break;
                case GL_RGBA:
                    name = "RGBA8";
                    break;
                case GL_LUMINANCE:
                    name = "L8";
                    break;
                case GL_LUMINANCE_ALPHA:
                    name = "LA8";
                    break;
                default:
                    name = "pixel format " + std::to_string(image.getPixelFormat());
                    break;
            }

            return name + (image.getNumMipmapLevels() > 1 ? ", with mips" : ", one level");
        }

        /// A pass's texture matrix for `unit`, as the `uv * xy + zw` the shader wants.
        ///
        /// OpenSceneGraph hands the matrix to GLSL transposed — it stores rows where GLSL reads
        /// columns — so what a shader multiplies its coordinate by is the transpose of what is here,
        /// and the translation it picks up is this matrix's last row.
        osg::Vec4f getTextureTransform(const osg::StateSet& stateSet, unsigned int unit)
        {
            // Terrain binds two units and no more, so the names are spelled rather than built.
            static constexpr std::array<std::string_view, 2> sNames{ "texMat0", "texMat1" };
            assert(unit < sNames.size());

            const osg::Uniform* uniform = stateSet.getUniform(std::string(sNames[unit]));
            osg::Matrixf matrix;
            if (uniform == nullptr || !uniform->get(matrix))
                return osg::Vec4f(1.0f, 1.0f, 0.0f, 0.0f);

            return osg::Vec4f(matrix(0, 0), matrix(1, 1), matrix(3, 0), matrix(3, 1));
        }

        /// The weights of one blend map, as floats in row order.
        ///
        /// `ESMTerrain` builds these as one byte per texel in `GL_ALPHA`, which is a hundred bytes
        /// for a chunk; widening them costs a few kilobytes a cell and saves requiring 8-bit storage
        /// of the device for the sake of it.
        void readMask(const osg::Image& image, std::vector<float>& weights)
        {
            weights.clear();
            weights.reserve(static_cast<std::size_t>(image.s()) * image.t());
            for (int row = 0; row < image.t(); ++row)
                for (int column = 0; column < image.s(); ++column)
                    weights.push_back(image.getColor(column, row).a());
        }

        /// How much a material's emissive colour is worth, which a model can scale.
        ///
        /// `NifOsg` only attaches the uniform where the model asked for something other than one, so
        /// its absence is the default rather than a missing value.
        float getEmissiveMult(const osg::StateSet& stateSet)
        {
            const osg::Uniform* uniform = stateSet.getUniform("emissiveMult");
            float value = 1.0f;
            if (uniform != nullptr && uniform->get(value))
                return value;

            return 1.0f;
        }

        float getAlphaRef(const osg::StateSet& stateSet)
        {
            // The shader visitor turns the fixed-function alpha test into this uniform and removes
            // the attribute, so the uniform is the surviving record of a cutout.
            const osg::Uniform* uniform = stateSet.getUniform("alphaRef");
            float value = 0.0f;
            if (uniform != nullptr && uniform->get(value))
                return value;

            return 0.0f;
        }
    }

    ExtractionStats& ExtractionStats::operator+=(const ExtractionStats& other)
    {
        for (const auto& [format, count] : other.mTextureFormats)
            mTextureFormats[format] += count;

        mMeshesAdded += other.mMeshesAdded;
        mMeshesReused += other.mMeshesReused;
        mMaterialsAdded += other.mMaterialsAdded;
        mMaterialsReused += other.mMaterialsReused;
        mInstances += other.mInstances;
        mSkippedDeformed += other.mSkippedDeformed;
        mSkippedEmpty += other.mSkippedEmpty;
        return *this;
    }

    namespace
    {
        /// Walks the graph and hands every geometry it meets to the extractor.
        class ExtractionVisitor : public osg::NodeVisitor
        {
        public:
            ExtractionVisitor(SceneExtractor& extractor, const osg::Matrixf& root, ExtractionStats& stats)
                : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN)
                , mExtractor(extractor)
                , mRoot(root)
                , mStats(stats)
            {
            }

            void apply(osg::Drawable& drawable) override
            {
                osg::Geometry* geometry = drawable.asGeometry();
                if (geometry == nullptr)
                {
                    ++mStats.mSkippedDeformed;
                    return;
                }

                mExtractor.addDrawable(*geometry, getNodePath(), mRoot, mStats);
            }

        private:
            SceneExtractor& mExtractor;
            const osg::Matrixf mRoot;
            ExtractionStats& mStats;
        };
    }

    ExtractionStats SceneExtractor::extract(const osg::Node& node, const osg::Matrixf& transform)
    {
        ExtractionStats stats;
        ExtractionVisitor visitor(*this, transform, stats);

        // OSG's visitor API is non-const even for a visitor that only reads, which this one does.
        const_cast<osg::Node&>(node).accept(visitor);

        return stats;
    }

    void SceneExtractor::addDrawable(
        const osg::Geometry& geometry, const osg::NodePath& path, const osg::Matrixf& root, ExtractionStats& stats)
    {
        const Rtx::Index mesh = resolveMesh(geometry, stats);
        if (mesh == Rtx::sNoIndex)
            return;

        // Terrain keeps its material on the drawable rather than on the graph, so it is asked first
        // and the state-set walk never sees a chunk.
        const auto* terrain = dynamic_cast<const Terrain::TerrainDrawable*>(&geometry);
        const Rtx::Index material
            = terrain != nullptr ? resolveTerrainMaterial(*terrain, stats) : resolveMaterial(path, stats);

        mScene.addInstance(Rtx::MeshInstance{
            .mTransform = osg::Matrixf(osg::computeLocalToWorld(path)) * root,
            .mMesh = mesh,
            .mMaterial = material,
        });
        ++stats.mInstances;
    }

    Rtx::Index SceneExtractor::resolveTerrainMaterial(const Terrain::TerrainDrawable& terrain, ExtractionStats& stats)
    {
        const Terrain::TerrainDrawable::PassVector& passes = terrain.getPasses();
        if (passes.empty())
            return Rtx::sNoIndex;

        // The first pass is as good an identity as the chunk itself and is already a state set, so
        // terrain shares the material map with everything else.
        const osg::StateSet* identity = passes.front().get();
        const auto known = mMaterials.find(identity);
        if (known != mMaterials.end())
        {
            ++stats.mMaterialsReused;
            return known->second;
        }

        Rtx::Material material;
        material.mKind = Rtx::MaterialKind::Terrain;
        material.mLayerOffset = static_cast<Rtx::Index>(mScene.getLayers().size());

        for (const osg::ref_ptr<osg::StateSet>& pass : passes)
        {
            const osg::Texture2D* diffuse = getTexture(*pass, 0);
            if (diffuse == nullptr || diffuse->getImage(0) == nullptr || diffuse->getImage(0)->getFileName().empty())
                continue;

            Rtx::MaterialLayer layer;
            layer.mDiffuse = mScene.addTexture(VFS::Path::Normalized(diffuse->getImage(0)->getFileName()));
            layer.mDiffuseTransform = getTextureTransform(*pass, 0);
            ++stats.mTextureFormats[describeFormat(*diffuse->getImage(0))];

            // A chunk of a single ground type is given no blend map at all, and stays at full weight.
            const osg::Texture2D* mask = getTexture(*pass, 1);
            if (mask != nullptr && mask->getImage(0) != nullptr)
            {
                const osg::Image& image = *mask->getImage(0);
                readMask(image, mMaskScratch);
                layer.mMaskOffset = mScene.addMask(mMaskScratch);
                layer.mMaskWidth = static_cast<std::uint16_t>(image.s());
                layer.mMaskHeight = static_cast<std::uint16_t>(image.t());
                layer.mMaskTransform = getTextureTransform(*pass, 1);
            }

            mScene.addLayer(layer);
            ++material.mLayerCount;
        }

        if (material.mLayerCount == 0)
            return Rtx::sNoIndex;

        const Rtx::Index index = mScene.addMaterial(material);
        mMaterials.emplace(identity, index);
        ++stats.mMaterialsAdded;
        return index;
    }

    Rtx::Index SceneExtractor::resolveMesh(const osg::Geometry& geometry, ExtractionStats& stats)
    {
        const auto known = mMeshes.find(&geometry);
        if (known != mMeshes.end())
        {
            ++stats.mMeshesReused;
            return known->second;
        }

        const auto* positions = dynamic_cast<const osg::Vec3Array*>(geometry.getVertexArray());
        if (positions == nullptr || positions->empty())
        {
            ++stats.mSkippedEmpty;
            return Rtx::sNoIndex;
        }

        mIndexScratch.clear();
        osg::TriangleIndexFunctor<TriangleCollector> collector;
        collector.mIndices = &mIndexScratch;
        geometry.accept(collector);

        if (mIndexScratch.empty())
        {
            ++stats.mSkippedEmpty;
            return Rtx::sNoIndex;
        }

        // A per-vertex array is the only binding worth carrying: anything coarser describes the whole
        // drawable, and a ray tracer shading a hit point wants a value it can interpolate.
        std::span<const osg::Vec3f> normals;
        const auto* normalArray = dynamic_cast<const osg::Vec3Array*>(geometry.getNormalArray());
        if (normalArray != nullptr && normalArray->size() == positions->size())
            normals = std::span(normalArray->asVector());

        std::span<const osg::Vec2f> texCoords;
        const auto* texCoordArray = dynamic_cast<const osg::Vec2Array*>(geometry.getTexCoordArray(0));
        if (texCoordArray != nullptr && texCoordArray->size() == positions->size())
            texCoords = std::span(texCoordArray->asVector());

        const Rtx::Index mesh = mScene.addMesh(std::span(positions->asVector()), normals, texCoords, mIndexScratch);
        mMeshes.emplace(&geometry, mesh);
        ++stats.mMeshesAdded;
        return mesh;
    }

    Rtx::Index SceneExtractor::resolveMaterial(const osg::NodePath& path, ExtractionStats& stats)
    {
        const osg::StateSet* own = findOwnStateSet(path);
        if (own == nullptr)
            return Rtx::sNoIndex;

        const auto known = mMaterials.find(own);
        if (known != mMaterials.end())
        {
            ++stats.mMaterialsReused;
            return known->second;
        }

        Rtx::Material material;

        if (const osg::StateSet* textured = findTexturedStateSet(path))
        {
            for (unsigned int unit = 0; unit < textured->getTextureAttributeList().size(); ++unit)
            {
                const osg::Texture2D* texture = getTexture(*textured, unit);
                if (texture == nullptr || texture->getImage(0) == nullptr)
                    continue;

                const std::string& file = texture->getImage(0)->getFileName();
                if (file.empty())
                    continue;

                const Rtx::Index index = mScene.addTexture(VFS::Path::Normalized(file));
                ++stats.mTextureFormats[describeFormat(*texture->getImage(0))];
                const std::string_view role = getTextureRole(*textured, unit);

                // Unit 0 without a role is the diffuse slot: that is where `NifOsg` puts the base map
                // when the shader visitor has not run to label it.
                if (role == "diffuseMap" || (role.empty() && unit == 0))
                    material.mDiffuse = index;
                else if (role == "normalMap" || role == "normalHeightMap")
                    material.mNormal = index;
                else if (role == "emissiveMap")
                    material.mEmissive = index;
            }
        }

        material.mAlphaRef = getAlphaRef(*own);
        if (isBlended(*own))
            material.mAlphaMode = Rtx::AlphaMode::Blend;
        else if (material.mAlphaRef > 0.0f)
            material.mAlphaMode = Rtx::AlphaMode::Cutout;

        material.mTwoSided = isTwoSided(*own);

        const auto* colours = dynamic_cast<const osg::Material*>(own->getAttribute(osg::StateAttribute::MATERIAL));
        if (colours != nullptr)
        {
            material.mDiffuseColour = colours->getDiffuse(osg::Material::FRONT);

            // Folded together here because the game's own shader only ever uses their product.
            const osg::Vec4f emissive = colours->getEmission(osg::Material::FRONT) * getEmissiveMult(*own);
            material.mEmissiveColour = osg::Vec3f(emissive.x(), emissive.y(), emissive.z());
        }

        const Rtx::Index index = mScene.addMaterial(material);
        mMaterials.emplace(own, index);
        ++stats.mMaterialsAdded;
        return index;
    }
}
