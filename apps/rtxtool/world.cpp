#include "world.hpp"

#include <algorithm>
#include <charconv>
#include <vector>

#include <boost/program_options/variables_map.hpp>

#include <components/debug/debuglog.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/esm3/loadland.hpp>
#include <components/esmloader/lessbyid.hpp>
#include <components/esmloader/load.hpp>
#include <components/esmloader/record.hpp>
#include <components/fallback/fallback.hpp>
#include <components/fallback/validate.hpp>
#include <components/files/collections.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/files/conversion.hpp>
#include <components/misc/convert.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/sceneutil/lightmanager.hpp>
#include <components/sceneutil/shadow.hpp>
#include <components/settings/values.hpp>
#include <components/shader/shadermanager.hpp>
#include <components/terrain/terraingrid.hpp>
#include <components/vfs/registerarchives.hpp>

namespace RtxTool
{
    namespace
    {
        namespace bpo = boost::program_options;

        using StringsVector = std::vector<std::string>;

        /// Nothing here is drawn, so caching a resource past its last use only costs memory.
        constexpr double sExpiryDelay = 0;

        /// What the shader visitor is told a GPU offers.
        ///
        /// It runs on every model OpenMW loads and needs a number to fit texture slots into. Without
        /// a context there is nothing to ask, and the value only decides how many slots it is willing
        /// to use — the roles it labels them with, which is what this tool reads, are the same.
        constexpr int sAssumedTextureUnits = 32;

        /// The defines every shader template expects to have been told before it can be assembled.
        ///
        /// The game fills these in from a realised GL context. There is none here, and the values do
        /// not matter: nothing this tool builds will be compiled by a driver. What matters is that
        /// every name the templates reference is defined, because an undefined one throws and takes
        /// the model that triggered it with it.
        Shader::ShaderManager::DefineMap makeGlobalDefines(Resource::ResourceSystem& resourceSystem)
        {
            Shader::ShaderManager::DefineMap defines = Shader::getDefaultDefines();

            for (const auto& [name, value] : SceneUtil::ShadowManager::getShadowsDisabledDefines())
                defines[name] = value;

            const osg::ref_ptr<SceneUtil::LightManager> lights
                = new SceneUtil::LightManager(SceneUtil::LightSettings{}, &resourceSystem);
            for (const auto& [name, value] : lights->getLightDefines())
                defines[name] = value;

            return defines;
        }

        ToUTF8::Utf8Encoder makeEncoder(const bpo::variables_map& variables)
        {
            const std::string encoding(variables["encoding"].as<std::string>());
            Log(Debug::Info) << ToUTF8::encodingUsingMessage(encoding);
            return ToUTF8::Utf8Encoder(ToUTF8::calculateEncoding(encoding));
        }

        Files::Collections makeFileCollections(
            Files::ConfigurationManager& config, const bpo::variables_map& variables, const std::filesystem::path& res)
        {
            Files::PathContainer dataDirs(
                Files::asPathContainer(variables["data"].as<Files::MaybeQuotedPathContainer>()));

            auto local = variables["data-local"].as<Files::MaybeQuotedPathContainer::value_type>();
            if (!local.empty())
                dataDirs.push_back(std::move(local));

            config.filterOutNonExistingPaths(dataDirs);
            dataDirs.insert(dataDirs.begin(), res / "vfs");
            return Files::Collections(dataDirs);
        }

        EsmLoader::EsmData loadContent(const bpo::variables_map& variables, const Files::Collections& fileCollections,
            ESM::ReadersCache& readers, ToUTF8::Utf8Encoder& encoder)
        {
            StringsVector contentFiles{ "builtin.omwscripts" };
            const auto& configured = variables["content"].as<StringsVector>();
            contentFiles.insert(contentFiles.end(), configured.begin(), configured.end());

            EsmLoader::Query query;
            query.mLoadCells = true;
            query.mLoadGameSettings = true;
            query.mLoadLands = true;
            query.mLoadLandTextures = true;
            // Everything that names a model, because everything a cell places has to be rendered:
            // a room missing its lamps and its bookshelves is not the room.
            query.mModels = EsmLoader::allModelRecords();
            return EsmLoader::loadEsmData(query, contentFiles, fileCollections, readers, &encoder);
        }

        /// A cell reference reduced to what placing a model needs.
        struct PlacedRef
        {
            ESM::RefNum mRefNum;
            ESM::RefId mRefId;
            ESM::RecNameInts mType;
            ESM::Position mPos;
            float mScale;
        };

        osg::Matrixf makeTransform(const PlacedRef& ref)
        {
            // Scale, then rotate, then translate. The rotation is Misc::Convert's, which applies the
            // reference's Euler angles Z first — the order the original engine used, and not the one
            // a reader of the record would assume.
            osg::Matrixf transform;
            transform.makeRotate(Misc::Convert::makeOsgQuat(ref.mPos));
            transform.preMultScale(osg::Vec3f(ref.mScale, ref.mScale, ref.mScale));
            transform.setTrans(ref.mPos.asVec3());
            return transform;
        }
    }

    World::World(Files::ConfigurationManager& config, const bpo::variables_map& variables,
        const std::filesystem::path& resourcePath)
        : mEncoder(makeEncoder(variables))
        , mFileCollections(makeFileCollections(config, variables, resourcePath))
        , mEsmData(loadContent(variables, mFileCollections, mReaders, mEncoder))
    {
        Fallback::Map::init(variables["fallback"].as<Fallback::FallbackMap>().mMap);

        const auto& archives = variables["fallback-archive"].as<StringsVector>();
        VFS::registerArchives(&mVfs, mFileCollections, archives, true, &mEncoder.getStatelessEncoder());

        mResourceSystem
            = std::make_unique<Resource::ResourceSystem>(&mVfs, sExpiryDelay, &mEncoder.getStatelessEncoder());

        Resource::SceneManager& sceneManager = *mResourceSystem->getSceneManager();

        // The shader visitor runs on every model as it loads and is what labels a texture slot
        // "diffuseMap" or "normalMap". Those labels are the only record of what a texture is for, so
        // the tool needs it to run — and it throws when a program will not build, which takes the
        // whole model down with it. Nothing here will ever be compiled by a driver.
        sceneManager.setShaderPath(resourcePath / "shaders");
        sceneManager.getShaderManager().setMaxTextureUnits(sAssumedTextureUnits);
        // Taken by non-const reference: the shader manager reserves the right to add to it.
        Shader::ShaderManager::DefineMap globalDefines = makeGlobalDefines(*mResourceSystem);
        sceneManager.getShaderManager().setGlobalDefines(globalDefines);
        sceneManager.setAutoUseNormalMaps(Settings::shaders().mAutoUseObjectNormalMaps);
        sceneManager.setNormalMapPattern(Settings::shaders().mNormalMapPattern);
        sceneManager.setNormalHeightMapPattern(Settings::shaders().mNormalHeightMapPattern);
        sceneManager.setAutoUseSpecularMaps(Settings::shaders().mAutoUseObjectSpecularMaps);
        sceneManager.setSpecularMapPattern(Settings::shaders().mSpecularMapPattern);
    }

    osg::ref_ptr<osg::Node> World::buildTerrain(const ESM::Cell& cell)
    {
        if (!cell.isExterior())
            return nullptr;

        if (mTerrain == nullptr)
        {
            mTerrainStorage = std::make_unique<TerrainStorage>(mVfs, mEsmData);
            mTerrainParent = new osg::Group;

            // `Terrain::World` hangs a pre-render camera off this to build composite maps. Nothing
            // here ever draws, so the camera is inert and the chunks come out with their blend maps
            // instead, which is what a ray tracer wants anyway.
            mCompileRoot = new osg::Group;

            mTerrain = std::make_unique<Terrain::TerrainGrid>(mTerrainParent, mCompileRoot, mResourceSystem.get(),
                mTerrainStorage.get(), ~0u, ESM::Cell::sDefaultWorldspaceId, sExpiryDelay);
        }

        mTerrain->loadCell(cell.getGridX(), cell.getGridY());
        return mTerrainParent;
    }

    Resource::SceneManager& World::getSceneManager()
    {
        return *mResourceSystem->getSceneManager();
    }

    Resource::ImageManager& World::getImageManager()
    {
        return *mResourceSystem->getImageManager();
    }

    World::~World() = default;

    const ESM::Cell* World::findCell(std::string_view spec) const
    {
        // Morrowind's own addressing: a pair of integers is an exterior, anything else is a name.
        const std::size_t comma = spec.find(',');
        if (comma != std::string_view::npos)
        {
            int x = 0;
            int y = 0;
            const std::string_view first = spec.substr(0, comma);
            const std::string_view second = spec.substr(comma + 1);
            const auto parsedX = std::from_chars(first.data(), first.data() + first.size(), x);
            const auto parsedY = std::from_chars(second.data(), second.data() + second.size(), y);

            if (parsedX.ec == std::errc() && parsedX.ptr == first.data() + first.size() && parsedY.ec == std::errc()
                && parsedY.ptr == second.data() + second.size())
            {
                for (const ESM::Cell& cell : mEsmData.mCells)
                    if (cell.isExterior() && cell.getGridX() == x && cell.getGridY() == y)
                        return &cell;

                return nullptr;
            }
        }

        for (const ESM::Cell& cell : mEsmData.mCells)
            if (!cell.isExterior() && Misc::StringUtils::ciEqual(cell.mName, spec))
                return &cell;

        return nullptr;
    }

    World::SkippedObjects World::forEachObject(const ESM::Cell& cell, const std::function<void(const Object&)>& handle)
    {
        // A later content file can move or delete a reference an earlier one placed, so the refs are
        // gathered and reduced by reference number before any of them is drawn. Skipping this draws
        // the mod's version of an object on top of the original's.
        EsmLoader::Records<PlacedRef> gathered;
        for (std::size_t i = 0; i < cell.mContextList.size(); ++i)
        {
            const ESM::ReadersCache::BusyItem reader
                = mReaders.get(static_cast<std::size_t>(cell.mContextList[i].index));
            cell.restore(*reader, static_cast<int>(i));

            ESM::CellRef ref;
            bool deleted = false;
            while (ESM::Cell::getNextRef(*reader, ref, deleted))
            {
                const auto type = std::lower_bound(
                    mEsmData.mRefIdTypes.begin(), mEsmData.mRefIdTypes.end(), ref.mRefID, EsmLoader::LessById{});
                const ESM::RecNameInts recordType = (type == mEsmData.mRefIdTypes.end() || type->mId != ref.mRefID)
                    ? ESM::RecNameInts{}
                    : type->mType;

                gathered.emplace_back(
                    deleted, PlacedRef{ ref.mRefNum, std::move(ref.mRefID), recordType, ref.mPos, ref.mScale });
            }
        }

        const std::vector<PlacedRef> refs = EsmLoader::prepareRecords(
            gathered, [](const EsmLoader::Record<PlacedRef>& v) -> ESM::RefNum { return v.mValue.mRefNum; });

        SkippedObjects skipped;
        for (const PlacedRef& ref : refs)
        {
            if (ref.mType == ESM::RecNameInts{})
            {
                ++skipped.mUnknownType;
                continue;
            }

            VFS::Path::Normalized model(EsmLoader::getModel(mEsmData, ref.mRefId, ref.mType));
            if (model.empty())
            {
                ++skipped.mNoModel;
                continue;
            }

            if (ref.mType != ESM::REC_STAT)
                model = Misc::ResourceHelpers::correctActorModelPath(model, &mVfs);

            handle(Object{
                .mModel = Misc::ResourceHelpers::correctMeshPath(model),
                .mTransform = makeTransform(ref),
                .mLight = ref.mType == ESM::REC_LIGH ? EsmLoader::find<ESM::Light>(mEsmData, ref.mRefId) : nullptr,
            });
        }

        return skipped;
    }
}
