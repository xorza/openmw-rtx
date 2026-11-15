#pragma once

#if defined(_WIN32) || defined(__WINDOWS__)

#include <filesystem>
#include <vector>

/**
 * \namespace Files
 */
namespace Files
{

    /**
     * \struct WindowsPath
     */
    struct WindowsPath
    {
        /**
         * \brief WindowsPath constructor.
         *
         * \param [in] applicationName - The name of the application.
         */
        explicit WindowsPath(const std::string& applicationName);

        /**
         * \brief Returns user path i.e.:
         * "X:\Documents And Settings\<User name>\My Documents\My Games\"
         *
         * \return std::filesystem::path
         */
        std::filesystem::path getUserConfigPath() const;

        std::filesystem::path getUserDataPath() const;

        /**
         * \brief Returns an empty path
         *
         * \return std::filesystem::path
         */
        std::filesystem::path getGlobalConfigPath() const;

        /**
         * \brief Return local path which is a location where
         * an application was started
         *
         * \return std::filesystem::path
         */
        std::filesystem::path getLocalPath() const;

        /**
         * \brief
         *
         * \return std::filesystem::path
         */
        std::filesystem::path getCachePath() const;

        /// The user's data kept on this machine alone, not synced with the profile: what a program
        /// downloads and loads, such as the DLSS library.
        std::filesystem::path getUserLocalDataPath() const;

        /**
         * \brief Return same path like getGlobalPath
         *
         * \return std::filesystem::path
         */
        std::filesystem::path getGlobalDataPath() const;

        /**
         * \brief Gets the paths of any installed Morrowind versions we can find.
         *
         * \return std::vector<std::filesystem::path>
         */
        std::vector<std::filesystem::path> getInstallPaths() const;

        std::string mName;
    };

} /* namespace Files */

#endif /* defined(_WIN32) || defined(__WINDOWS__) */
