#include "postprocessingbindings.hpp"

#include <cstddef>
#include <cstdint>
#include <format>
#include <string_view>
#include <type_traits>

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/world.hpp>
#include <apps/openmw/mwrender/postprocessor.hpp>
#include <apps/openmw/mwrender/renderer.hpp>
#include <apps/openmw/mwrender/renderingmanager.hpp>
#include <apps/openmw/mwrender/rendersupport.hpp>
#include <components/debug/debuglog.hpp>
#include <components/lua/util.hpp>
#include <components/misc/convert.hpp>

#include "MyGUI_LanguageManager.h"
#include "context.hpp"
#include "luamanagerimp.hpp"

namespace
{
    std::string getLocalizedMyGUIString(std::string_view unlocalized)
    {
        return MyGUI::LanguageManager::getInstance().replaceTags(MyGUI::UString(unlocalized)).asUTF8();
    }
}

namespace MWLua
{
    struct Shader;
}

namespace sol
{
    template <>
    struct is_automagical<MWLua::Shader> : std::false_type
    {
    };
}

namespace MWLua
{
    struct Shader
    {
        std::shared_ptr<Fx::Technique> mShader;

        // A renderer with no chain, which the ray tracer is, loads the name alone: the script runs,
        // and its technique draws nothing, as under a chain that is switched off
        std::string mRequested;

        explicit Shader(std::shared_ptr<Fx::Technique> shader)
            : mShader(std::move(shader))
        {
        }

        explicit Shader(std::string_view requested)
            : mRequested(requested)
        {
        }

        std::string toString() const
        {
            if (!mShader)
                return "Shader(nil)";

            return std::format("Shader({}, {})", mShader->getName(), mShader->getFileName().value());
        }

        enum
        {
            Action_None,
            Action_Enable,
            Action_Disable
        } mQueuedAction = Action_None;
    };

    namespace
    {
        /// Why the renderer has no post-processing chain, or nothing where it has one: the seam's answer, as every
        /// other caller asks it (`MWRender::RenderSupport::declinedSetting`).
        std::string_view declinedChain()
        {
            return MWBase::Environment::get()
                .getWorld()
                ->getRenderingManager()
                ->getRenderer()
                .support()
                .declinedSetting("Post Processing", "enabled");
        }

        // Lua vectors are glm; shader uniforms are still osg.
        template <class T>
        struct UniformType
        {
            using Type = T;
        };
        template <>
        struct UniformType<glm::vec2>
        {
            using Type = osg::Vec2f;
        };
        template <>
        struct UniformType<glm::vec3>
        {
            using Type = osg::Vec3f;
        };
        template <>
        struct UniformType<glm::vec4>
        {
            using Type = osg::Vec4f;
        };

        template <class T>
        typename UniformType<T>::Type toUniform(const T& value)
        {
            if constexpr (std::is_same_v<T, typename UniformType<T>::Type>)
                return value;
            else
                return Misc::Convert::toOsg(value);
        }

        template <class T>
        auto getSetter(const Context& context)
        {
            return [context](const Shader& shader, const std::string& name, const T& value) {
                if (!shader.mShader)
                    return;

                context.mLuaManager->addAction(
                    [=] {
                        MWBase::Environment::get().getWorld()->getPostProcessor()->setUniform(
                            shader.mShader, name, toUniform(value));
                    },
                    "SetUniformShaderAction");
            };
        }

        template <class T>
        auto getArraySetter(const Context& context)
        {
            return [context](const Shader& shader, const std::string& name, const sol::table& table) {
                if (!shader.mShader)
                    return;

                auto targetSize
                    = MWBase::Environment::get().getWorld()->getPostProcessor()->getUniformSize(shader.mShader, name);

                if (!targetSize.has_value())
                    throw std::runtime_error(std::format("Failed setting uniform array '{}'", name));

                if (*targetSize != table.size())
                    throw std::runtime_error(
                        std::format("Mismatching uniform array size, got {} expected {}", table.size(), *targetSize));

                std::vector<typename UniformType<T>::Type> values;
                values.reserve(*targetSize);

                for (std::size_t i = 0; i < *targetSize; ++i)
                {
                    sol::object obj = table[LuaUtil::toLuaIndex(static_cast<std::int64_t>(i))];
                    if (!obj.is<T>())
                        throw std::runtime_error("Invalid type for uniform array");
                    values.push_back(toUniform(obj.as<T>()));
                }

                context.mLuaManager->addAction(
                    [shader, name, values = std::move(values)] {
                        MWBase::Environment::get().getWorld()->getPostProcessor()->setUniform(
                            shader.mShader, name, values);
                    },
                    "SetUniformShaderAction");
            };
        }
    }

    sol::table initPostprocessingPackage(const Context& context)
    {
        sol::state_view lua = context.sol();
        sol::table api(lua, sol::create);

        {
            sol::usertype<Shader> shader = lua.new_usertype<Shader>("Shader");
            shader[sol::meta_function::to_string] = [](const Shader& self) { return self.toString(); };

            shader["enable"] = [context](Shader& self, sol::optional<std::int32_t> optPos) {
                std::optional<std::int32_t> pos = std::nullopt;
                if (optPos)
                    pos = optPos.value();

                if (!self.mShader)
                    return;

                if (self.mShader && self.mShader->isValid())
                    self.mQueuedAction = Shader::Action_Enable;

                context.mLuaManager->addAction([=, &self] {
                    self.mQueuedAction = Shader::Action_None;

                    if (MWBase::Environment::get().getWorld()->getPostProcessor()->enableTechnique(self.mShader, pos)
                        == MWRender::PostProcessor::Status_Error)
                        throw std::runtime_error(std::format("Failed enabling shader '{}'", self.mShader->getName()));
                });
            };

            shader["disable"] = [context](Shader& self) {
                if (!self.mShader)
                    return;

                self.mQueuedAction = Shader::Action_Disable;

                context.mLuaManager->addAction([&] {
                    self.mQueuedAction = Shader::Action_None;

                    if (MWBase::Environment::get().getWorld()->getPostProcessor()->disableTechnique(self.mShader)
                        == MWRender::PostProcessor::Status_Error)
                        throw std::runtime_error(std::format("Failed disabling shader '{}'", self.mShader->getName()));
                });
            };

            shader["isEnabled"] = [](const Shader& self) {
                if (!self.mShader)
                    return false;
                if (self.mQueuedAction == Shader::Action_Enable)
                    return true;
                else if (self.mQueuedAction == Shader::Action_Disable)
                    return false;
                return MWBase::Environment::get().getWorld()->getPostProcessor()->isTechniqueEnabled(self.mShader);
            };

            shader["name"] = sol::readonly_property([](const Shader& self) {
                return self.mShader ? getLocalizedMyGUIString(self.mShader->getName()) : self.mRequested;
            });
            shader["author"] = sol::readonly_property([](const Shader& self) {
                return self.mShader ? getLocalizedMyGUIString(self.mShader->getAuthor()) : std::string();
            });
            shader["description"] = sol::readonly_property([](const Shader& self) {
                return self.mShader ? getLocalizedMyGUIString(self.mShader->getDescription()) : std::string();
            });
            shader["version"] = sol::readonly_property([](const Shader& self) {
                return self.mShader ? getLocalizedMyGUIString(self.mShader->getVersion()) : std::string();
            });

            shader["setBool"] = getSetter<bool>(context);
            shader["setFloat"] = getSetter<float>(context);
            shader["setInt"] = getSetter<std::int32_t>(context);
            shader["setVector2"] = getSetter<glm::vec2>(context);
            shader["setVector3"] = getSetter<glm::vec3>(context);
            shader["setVector4"] = getSetter<glm::vec4>(context);

            shader["setFloatArray"] = getArraySetter<float>(context);
            shader["setIntArray"] = getArraySetter<std::int32_t>(context);
            shader["setVector2Array"] = getArraySetter<glm::vec2>(context);
            shader["setVector3Array"] = getArraySetter<glm::vec3>(context);
            shader["setVector4Array"] = getArraySetter<glm::vec4>(context);
        }

        api["load"] = [](const std::string& name) {
            if (const std::string_view declined = declinedChain(); !declined.empty())
            {
                [[maybe_unused]] static const bool told = [declined] {
                    Log(Debug::Info) << "A script's post-processing shaders load and draw nothing: " << declined;
                    return true;
                }();
                return Shader(std::string_view(name));
            }

            Shader shader{ MWBase::Environment::get().getWorld()->getPostProcessor()->loadTechnique(name, false) };

            if (!shader.mShader || !shader.mShader->isValid())
                throw std::runtime_error(std::format("Failed loading shader '{}'", name));

            return shader;
        };

        api["getChain"] = [context]() {
            sol::table chain(context.sol(), sol::create);
            if (!declinedChain().empty())
                return chain;

            for (const auto& shader : MWBase::Environment::get().getWorld()->getPostProcessor()->getChain())
            {
                // Don't expose internal shaders to the API, they should be invisible to the user
                if (shader->getInternal())
                    continue;
                chain.add(Shader(shader));
            }

            return chain;
        };

        return LuaUtil::makeReadOnly(api);
    }

}
