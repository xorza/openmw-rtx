#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <components/esm3/aipackage.hpp>

std::string_view bodyPartLabel(std::int32_t idx);
std::string_view meshPartLabel(std::int32_t idx);
std::string_view meshTypeLabel(std::int32_t idx);
std::string_view clothingTypeLabel(std::int32_t idx);
std::string_view armorTypeLabel(std::int32_t idx);
std::string_view dialogTypeLabel(std::int32_t idx);
std::string_view questStatusLabel(std::int32_t idx);
std::string_view creatureTypeLabel(std::int32_t idx);
std::string_view soundTypeLabel(std::int32_t idx);
std::string_view weaponTypeLabel(std::int32_t idx);

// This function's a bit different because the types are record types,
// not consecutive values.
std::string_view aiTypeLabel(ESM::AiPackageType type);

// This one's also a bit different, because it enumerates dialog
// select rule functions, not types.  Structurally, it still converts
// indexes to strings for display.
std::string_view ruleFunction(std::int32_t idx);

// The labels below here can all be loaded from GMSTs, but are not
// currently because among other things, that requires loading the
// GMSTs before dumping any of the records.

// If the data format supported ordered lists of GMSTs (post 1.0), the
// lists could define the valid values, their localization strings,
// and the indexes for referencing the types in other records in the
// database.  Then a single label function could work for all types.

std::string_view spellTypeLabel(std::int32_t idx);
std::string_view specializationLabel(std::int32_t idx);
std::string_view apparatusTypeLabel(std::int32_t idx);
std::string_view rangeTypeLabel(std::int32_t idx);
std::string_view schoolLabel(std::int32_t idx);
std::string_view enchantTypeLabel(std::int32_t idx);

// The are the flag functions that convert a bitmask into a list of
// human readble strings representing the set bits.

std::string bodyPartFlags(std::int32_t flags);
std::string cellFlags(std::int32_t flags);
std::string containerFlags(std::int32_t flags);
std::string creatureFlags(std::int32_t flags);
std::string enchantmentFlags(std::int32_t flags);
std::string factionFlags(std::int32_t flags);
std::string landFlags(std::uint32_t flags);
std::string creatureListFlags(std::int32_t flags);
std::string itemListFlags(std::int32_t flags);
std::string lightFlags(std::int32_t flags);
std::string magicEffectFlags(std::int32_t flags);
std::string npcFlags(std::int32_t flags);
std::string potionFlags(std::int32_t flags);
std::string raceFlags(std::int32_t flags);
std::string spellFlags(std::int32_t flags);
std::string weaponFlags(std::int32_t flags);

std::string recordFlags(std::uint32_t flags);

// Missing flags functions:
// aiServicesFlags, possibly more
