#pragma once

#include <cstddef>
#include <vector>

namespace MyGUI
{
    class IObject;
}

namespace Gui
{
    /// **Everything drawn with the interface's fonts, known by construction**: each user enters this
    /// as it is made and leaves as it is destroyed, so a refit after the fonts are rasterized again
    /// reaches every one. A font's texture is replaced by its rasterization, and a user a refit
    /// missed keeps the destroyed one (`docs/rtx/research/2026-10-31-mygui-font-lifetimes.md`).
    ///
    /// The texts are known through their factory: the fork's texts stand in MyGUI's sub-widget
    /// factories under MyGUI's names, `EditText` and `SimpleText` (`registerTextFactories`), so
    /// every text any widget, skin, popup or script makes is one. A walk of the widget tree would miss
    /// a skin's own children and the widgets on no layer, and nothing at the draw can catch a missed
    /// text: one whose glyphs are laid out is drawn from its render item's vertices without being
    /// asked, and a replaced atlas's address may be the new one's.
    ///
    /// Pinned: what it holds and the factories MyGUI keeps call back into it. It outlives every
    /// user, the last ones leaving at MyGUI's shutdown.
    class FontUsers
    {
    public:
        /// What the registry knows of a user.
        class Entry
        {
        public:
            Entry(const Entry&) = delete;
            Entry& operator=(const Entry&) = delete;

            /// Lets go of whatever holds a font's old texture, before any user is refitted.
            virtual void drop() {}

            /// Takes its fonts again, once every user has dropped.
            virtual void refit() = 0;

        protected:
            explicit Entry(FontUsers& users);
            virtual ~Entry();

        private:
            friend class FontUsers;

            FontUsers& mUsers;

            /// Where it stands among the registry's entries, so it leaves without a search.
            std::size_t mIndex;
        };

        FontUsers() = default;
        ~FontUsers();

        FontUsers(const FontUsers&) = delete;
        FontUsers& operator=(const FontUsers&) = delete;
        FontUsers(FontUsers&&) = delete;
        FontUsers& operator=(FontUsers&&) = delete;

        /// Stands the fork's texts in MyGUI's sub-widget factories, in place of MyGUI's own, each
        /// entering this as it is made: once the factory manager exists, and before a skin is loaded.
        void registerTextFactories();

        /// Every user drops, then every user is refitted: after a font is rasterized again, and
        /// before the next frame draws. No user of this registry is made or destroyed meanwhile,
        /// which an assert holds to; a refit may make users of another.
        void refit();

        std::size_t size() const { return mEntries.size(); }

    private:
        void makeText(MyGUI::IObject*& made);
        void makeSimpleText(MyGUI::IObject*& made);

        std::vector<Entry*> mEntries;
        bool mRefitting = false;
    };
}
