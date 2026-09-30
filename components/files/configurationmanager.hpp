#ifndef COMPONENTS_FILES_CONFIGURATIONMANAGER_HPP
#define COMPONENTS_FILES_CONFIGURATIONMANAGER_HPP

#include <optional>
#include <stack>
#include <string>
#include <string_view>
#include <vector>

#include <boost/program_options/value_semantic.hpp>

#include <components/files/collections.hpp>
#include <components/files/fixedpath.hpp>

// NOLINTBEGIN(readability-identifier-naming)
namespace boost::program_options
{
    class options_description;
    class variables_map;
}
// NOLINTEND(readability-identifier-naming)

/**
 * \namespace Files
 */
namespace Files
{
    inline constexpr std::string_view openmwCfgFile = "openmw.cfg";

    /**
     * \struct ConfigurationManager
     */
    struct ConfigurationManager
    {
        ConfigurationManager(bool silent = false); /// @param silent Emit log messages to cout?
        virtual ~ConfigurationManager();

        void readConfiguration(boost::program_options::variables_map& variables,
            const boost::program_options::options_description& description, bool quiet = false);

        void filterOutNonExistingPaths(Files::PathContainer& dataDirs) const;

        // Replaces tokens (`?local?`, `?global?`, etc.) in paths. Adds `basePath` prefix for relative paths.
        void processPath(std::filesystem::path& path, const std::filesystem::path& basePath) const;
        void processPaths(Files::PathContainer& dataDirs, const std::filesystem::path& basePath) const;
        void processPaths(
            boost::program_options::variables_map& variables, const std::filesystem::path& basePath) const;

        /**< Fixed paths */
        const std::filesystem::path& getGlobalPath() const;
        const std::filesystem::path& getLocalPath() const;

        const std::filesystem::path& getUserConfigPath() const;
        const std::filesystem::path& getUserDataPath() const;
        std::vector<std::filesystem::path> getInstallPaths() const;
        const std::vector<std::filesystem::path>& getActiveConfigPaths() const { return mActiveConfigPaths; }

        const std::filesystem::path& getCachePath() const;

        const std::filesystem::path& getLogPath() const { return getUserConfigPath(); }
        const std::filesystem::path& getScreenshotPath() const;

        static void addCommonOptions(boost::program_options::options_description& description);

    private:
        typedef Files::FixedPath<> FixedPathType;

        std::optional<boost::program_options::variables_map> loadConfig(
            const std::filesystem::path& path, const boost::program_options::options_description& description) const;

        void addExtraConfigDirs(
            std::stack<std::filesystem::path>& dirs, const boost::program_options::variables_map& variables) const;

        std::vector<std::filesystem::path> mActiveConfigPaths;

        FixedPathType mFixedPath;

        std::filesystem::path mUserDataPath;
        std::filesystem::path mScreenshotPath;

        bool mSilent;
    };

    boost::program_options::variables_map separateComposingVariables(boost::program_options::variables_map& variables,
        const boost::program_options::options_description& description);

    void mergeComposingVariables(boost::program_options::variables_map& first,
        boost::program_options::variables_map& second, const boost::program_options::options_description& description);

    void parseArgs(int argc, const char* const argv[], boost::program_options::variables_map& variables,
        const boost::program_options::options_description& description);

    void parseConfig(std::istream& stream, boost::program_options::variables_map& variables,
        const boost::program_options::options_description& description);

    class MaybeQuotedPath : public std::filesystem::path
    {
    };

    std::istream& operator>>(std::istream& istream, MaybeQuotedPath& value);

    typedef std::vector<MaybeQuotedPath> MaybeQuotedPathContainer;

    PathContainer asPathContainer(const MaybeQuotedPathContainer& value);

} /* namespace Files */

// **Instantiated once, in `configurationmanager.cpp`.** `typed_value::notify` stores through a pointer
// that is null unless a notifier was asked for, and GCC reports the store as a null dereference in
// every file that inlines it. Declared here, beside what every program's options read, it is compiled
// in that one file, which the one check is taken off.
extern template void boost::program_options::typed_value<std::string, char>::notify(const boost::any&) const;
extern template void boost::program_options::typed_value<std::vector<std::string>, char>::notify(
    const boost::any&) const;
extern template void boost::program_options::typed_value<Files::MaybeQuotedPathContainer, char>::notify(
    const boost::any&) const;

#endif /* COMPONENTS_FILES_CONFIGURATIONMANAGER_HPP */
