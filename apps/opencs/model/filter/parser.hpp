#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace CSMFilter
{
    class Node;
}

namespace CSMWorld
{
    class Data;
}

namespace CSMFilter
{
    struct Token;

    class Parser
    {
        std::shared_ptr<Node> mFilter;
        std::string mInput;
        std::int32_t mIndex;
        bool mError;
        const CSMWorld::Data& mData;

        Token getStringToken();

        Token getNumberToken();

        Token getNextToken();

        Token checkKeywords(const Token& token);
        ///< Turn string token into keyword token, if possible.

        std::shared_ptr<Node> parseImp(bool allowEmpty = false, bool ignoreOneShot = false);
        ///< Will return a null-pointer, if there is nothing more to parse.

        std::shared_ptr<Node> parseNAry(const Token& keyword);

        std::shared_ptr<Node> parseText();

        std::shared_ptr<Node> parseValue();

        void error();

    public:
        explicit Parser(const CSMWorld::Data& data);

        bool parse(const std::string& filter, bool allowPredefined = true);
        ///< Discards any previous calls to parse
        ///
        /// \return Success?

        std::shared_ptr<Node> getFilter() const;
        ///< Throws an exception if the last call to parse did not return true.
    };
}
