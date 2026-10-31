#pragma once

#include <cstddef>
#include <vector>

namespace MyGUI
{
    class IObject;
}

namespace Gui
{
    /// **Every text the interface draws, known by construction.** The fork's texts stand in MyGUI's
    /// sub-widget factories under MyGUI's names, `EditText` and `SimpleText`, and each one made enters
    /// this and each one destroyed leaves it: a rebind after the fonts are rasterized again reaches
    /// every text, whatever widget, skin, popup or script made it. A walk of the widget tree misses
    /// a skin's own children and the widgets on no layer, and a text a rebind misses keeps a
    /// destroyed texture (`docs/rtx/research/2026-10-31-mygui-font-lifetimes.md`). Nothing at the
    /// draw can catch one that was missed: a text whose glyphs are laid out already is drawn from
    /// its render item's vertices without being asked, and the replaced texture's address may be
    /// the new one's. So the factory is the guarantee, and `TextRegistryTest` holds every shipped
    /// text skin to it.
    ///
    /// Pinned: the factories MyGUI keeps call back into it. It outlives every text, each of which
    /// leaves it as it is destroyed, the last ones at MyGUI's shutdown.
    class TextRegistry
    {
    public:
        /// What the registry knows of a text.
        class Entry
        {
        public:
            Entry(const Entry&) = delete;
            Entry& operator=(const Entry&) = delete;

            /// Takes its font's texture again, and lays its glyphs out again.
            virtual void rebind() = 0;

        protected:
            explicit Entry(TextRegistry& registry);
            virtual ~Entry();

        private:
            friend class TextRegistry;

            TextRegistry& mRegistry;

            /// Where it stands among the registry's entries, so it leaves without a search.
            std::size_t mIndex;
        };

        TextRegistry() = default;
        ~TextRegistry();

        TextRegistry(const TextRegistry&) = delete;
        TextRegistry& operator=(const TextRegistry&) = delete;
        TextRegistry(TextRegistry&&) = delete;
        TextRegistry& operator=(TextRegistry&&) = delete;

        /// Stands the fork's texts in MyGUI's sub-widget factories, in place of MyGUI's own: once
        /// the factory manager exists, and before a skin is loaded.
        void registerFactories();

        /// Every text takes its font's texture again: after a font is rasterized again, and before
        /// the next frame draws.
        void rebind();

        std::size_t size() const { return mEntries.size(); }

    private:
        void makeText(MyGUI::IObject*& made);
        void makeSimpleText(MyGUI::IObject*& made);

        std::vector<Entry*> mEntries;
    };
}
