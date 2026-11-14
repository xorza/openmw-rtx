#pragma once

#include "errorhandler.hpp"

namespace Compiler
{
    /// \brief Error handler implementation: Ignore all error messages

    class NullErrorHandler : public ErrorHandler
    {
        void report(const std::string& message, const TokenLoc& loc, Type type) override {}
        ///< Report error to the user.

        void report(const std::string& message, Type type) override {}
        ///< Report a file related error
    };
}
