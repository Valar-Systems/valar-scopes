#include "IcaoCountry.h"

#include <cctype>
#include <cstdlib>

namespace {

// ICAO 24-bit address allocations by state (Annex 10, Vol III, Ch 9, App 1).
// Sorted ascending and non-overlapping, which is what the binary search below
// requires -- keep it that way when adding a block.
//
// The table is deliberately INCOMPLETE in one direction: the gaps between blocks
// are real (ICAO has not allocated them), and an address landing in one resolves
// to "" rather than to the nearest neighbour. Guessing would quietly file
// aircraft under the wrong flag, and a wrong country is worse than no country --
// it is a permanent, unremovable entry in someone's lifelist.
struct Block {
    uint32_t lo, hi;
    const char* name;
};

constexpr Block BLOCKS[] = {
    { 0x004000, 0x0043FF, "Zimbabwe" },
    { 0x006000, 0x006FFF, "Mozambique" },
    { 0x008000, 0x00FFFF, "South Africa" },
    { 0x010000, 0x017FFF, "Egypt" },
    { 0x018000, 0x01FFFF, "Libya" },
    { 0x020000, 0x027FFF, "Morocco" },
    { 0x028000, 0x02FFFF, "Tunisia" },
    { 0x030000, 0x0303FF, "Botswana" },
    { 0x032000, 0x032FFF, "Burundi" },
    { 0x034000, 0x034FFF, "Cameroon" },
    { 0x035000, 0x0353FF, "Comoros" },
    { 0x036000, 0x036FFF, "Congo" },
    { 0x038000, 0x038FFF, "Cote d'Ivoire" },
    { 0x03E000, 0x03EFFF, "Gabon" },
    { 0x040000, 0x040FFF, "Ethiopia" },
    { 0x042000, 0x042FFF, "Equatorial Guinea" },
    { 0x044000, 0x044FFF, "Ghana" },
    { 0x046000, 0x046FFF, "Guinea" },
    { 0x048000, 0x0483FF, "Guinea-Bissau" },
    { 0x04A000, 0x04A3FF, "Lesotho" },
    { 0x04C000, 0x04CFFF, "Kenya" },
    { 0x050000, 0x050FFF, "Liberia" },
    { 0x054000, 0x054FFF, "Madagascar" },
    { 0x058000, 0x058FFF, "Malawi" },
    { 0x05A000, 0x05A3FF, "Maldives" },
    { 0x05C000, 0x05CFFF, "Mali" },
    { 0x05E000, 0x05E3FF, "Mauritania" },
    { 0x060000, 0x0603FF, "Mauritius" },
    { 0x062000, 0x062FFF, "Niger" },
    { 0x064000, 0x064FFF, "Nigeria" },
    { 0x068000, 0x068FFF, "Uganda" },
    { 0x06A000, 0x06A3FF, "Qatar" },
    { 0x06C000, 0x06CFFF, "Central African Rep." },
    { 0x06E000, 0x06EFFF, "Rwanda" },
    { 0x070000, 0x070FFF, "Senegal" },
    { 0x074000, 0x0743FF, "Seychelles" },
    { 0x076000, 0x0763FF, "Sierra Leone" },
    { 0x078000, 0x078FFF, "Somalia" },
    { 0x07A000, 0x07A3FF, "Eswatini" },
    { 0x07C000, 0x07CFFF, "Sudan" },
    { 0x080000, 0x080FFF, "Tanzania" },
    { 0x084000, 0x084FFF, "Chad" },
    { 0x088000, 0x088FFF, "Togo" },
    { 0x08A000, 0x08AFFF, "Zambia" },
    { 0x08C000, 0x08CFFF, "DR Congo" },
    { 0x090000, 0x090FFF, "Angola" },
    { 0x094000, 0x0943FF, "Benin" },
    { 0x096000, 0x0963FF, "Cape Verde" },
    { 0x098000, 0x0983FF, "Djibouti" },
    { 0x09A000, 0x09AFFF, "Gambia" },
    { 0x09C000, 0x09CFFF, "Burkina Faso" },
    { 0x09E000, 0x09E3FF, "Sao Tome & Principe" },
    { 0x0A0000, 0x0A7FFF, "Algeria" },
    { 0x0A8000, 0x0A8FFF, "Bahamas" },
    { 0x0AA000, 0x0AA3FF, "Barbados" },
    { 0x0AB000, 0x0AB3FF, "Belize" },
    { 0x0AC000, 0x0ACFFF, "Colombia" },
    { 0x0AE000, 0x0AEFFF, "Costa Rica" },
    { 0x0B0000, 0x0B0FFF, "Cuba" },
    { 0x0B2000, 0x0B2FFF, "El Salvador" },
    { 0x0B4000, 0x0B4FFF, "Guatemala" },
    { 0x0B6000, 0x0B6FFF, "Guyana" },
    { 0x0B8000, 0x0B8FFF, "Haiti" },
    { 0x0BA000, 0x0BAFFF, "Honduras" },
    { 0x0BC000, 0x0BC3FF, "St Vincent & Gren." },
    { 0x0BE000, 0x0BEFFF, "Jamaica" },
    { 0x0C0000, 0x0C0FFF, "Nicaragua" },
    { 0x0C2000, 0x0C2FFF, "Panama" },
    { 0x0C4000, 0x0C4FFF, "Dominican Republic" },
    { 0x0C6000, 0x0C6FFF, "Trinidad & Tobago" },
    { 0x0C8000, 0x0C8FFF, "Suriname" },
    { 0x0CA000, 0x0CA3FF, "Antigua & Barbuda" },
    { 0x0CC000, 0x0CC3FF, "Grenada" },
    { 0x0D0000, 0x0D0FFF, "Mexico" },
    { 0x0D8000, 0x0DFFFF, "Venezuela" },
    { 0x100000, 0x1FFFFF, "Russia" },
    { 0x201000, 0x2013FF, "Namibia" },
    { 0x202000, 0x2023FF, "Eritrea" },
    { 0x300000, 0x33FFFF, "Italy" },
    { 0x340000, 0x37FFFF, "Spain" },
    { 0x380000, 0x3BFFFF, "France" },
    { 0x3C0000, 0x3FFFFF, "Germany" },
    // 0x400000-0x43FFFF is the UK block; the Channel Islands and Isle of Man
    // sub-allocations inside it are not split out (they are not ICAO states).
    { 0x400000, 0x43FFFF, "United Kingdom" },
    { 0x440000, 0x447FFF, "Austria" },
    { 0x448000, 0x44FFFF, "Belgium" },
    { 0x450000, 0x457FFF, "Bulgaria" },
    { 0x458000, 0x45FFFF, "Denmark" },
    { 0x460000, 0x467FFF, "Finland" },
    { 0x468000, 0x46FFFF, "Greece" },
    { 0x470000, 0x477FFF, "Hungary" },
    { 0x478000, 0x47FFFF, "Norway" },
    { 0x480000, 0x487FFF, "Netherlands" },
    { 0x488000, 0x48FFFF, "Poland" },
    { 0x490000, 0x497FFF, "Portugal" },
    { 0x498000, 0x49FFFF, "Czechia" },
    { 0x4A0000, 0x4A7FFF, "Romania" },
    { 0x4A8000, 0x4AFFFF, "Sweden" },
    { 0x4B0000, 0x4B7FFF, "Switzerland" },
    { 0x4B8000, 0x4BFFFF, "Turkey" },
    { 0x4C0000, 0x4C7FFF, "Serbia" },
    { 0x4C8000, 0x4C83FF, "Cyprus" },
    { 0x4CA000, 0x4CAFFF, "Ireland" },
    { 0x4CC000, 0x4CCFFF, "Iceland" },
    { 0x4D0000, 0x4D03FF, "Luxembourg" },
    { 0x4D2000, 0x4D23FF, "Malta" },
    { 0x4D4000, 0x4D43FF, "Monaco" },
    { 0x500000, 0x5003FF, "San Marino" },
    { 0x501000, 0x5013FF, "Albania" },
    { 0x501C00, 0x501FFF, "Croatia" },
    { 0x502C00, 0x502FFF, "Latvia" },
    { 0x503C00, 0x503FFF, "Lithuania" },
    { 0x504C00, 0x504FFF, "Moldova" },
    { 0x505C00, 0x505FFF, "Slovakia" },
    { 0x506C00, 0x506FFF, "Slovenia" },
    { 0x507C00, 0x507FFF, "Uzbekistan" },
    { 0x508000, 0x50FFFF, "Ukraine" },
    { 0x510000, 0x5103FF, "Belarus" },
    { 0x511000, 0x5113FF, "Estonia" },
    { 0x512000, 0x5123FF, "North Macedonia" },
    { 0x513000, 0x5133FF, "Bosnia & Herzegovina" },
    { 0x514000, 0x5143FF, "Georgia" },
    { 0x515000, 0x5153FF, "Tajikistan" },
    { 0x516000, 0x5163FF, "Montenegro" },
    { 0x600000, 0x6003FF, "Armenia" },
    { 0x600800, 0x600BFF, "Azerbaijan" },
    { 0x601000, 0x6013FF, "Kyrgyzstan" },
    { 0x601800, 0x601BFF, "Turkmenistan" },
    { 0x680000, 0x6803FF, "Bhutan" },
    { 0x681000, 0x6813FF, "Micronesia" },
    { 0x682000, 0x6823FF, "Mongolia" },
    { 0x683000, 0x6833FF, "Kazakhstan" },
    { 0x684000, 0x6843FF, "Palau" },
    { 0x700000, 0x700FFF, "Afghanistan" },
    { 0x702000, 0x702FFF, "Bangladesh" },
    { 0x704000, 0x704FFF, "Myanmar" },
    { 0x706000, 0x706FFF, "Kuwait" },
    { 0x708000, 0x708FFF, "Laos" },
    { 0x70A000, 0x70AFFF, "Nepal" },
    { 0x70C000, 0x70C3FF, "Oman" },
    { 0x70E000, 0x70EFFF, "Cambodia" },
    { 0x710000, 0x717FFF, "Saudi Arabia" },
    { 0x718000, 0x71FFFF, "South Korea" },
    { 0x720000, 0x727FFF, "North Korea" },
    { 0x728000, 0x72FFFF, "Iraq" },
    { 0x730000, 0x737FFF, "Iran" },
    { 0x738000, 0x73FFFF, "Israel" },
    { 0x740000, 0x747FFF, "Jordan" },
    { 0x748000, 0x74FFFF, "Lebanon" },
    { 0x750000, 0x757FFF, "Malaysia" },
    { 0x758000, 0x75FFFF, "Philippines" },
    { 0x760000, 0x767FFF, "Pakistan" },
    { 0x768000, 0x76FFFF, "Singapore" },
    { 0x770000, 0x777FFF, "Sri Lanka" },
    { 0x778000, 0x77FFFF, "Syria" },
    { 0x780000, 0x7BFFFF, "China" },
    { 0x7C0000, 0x7FFFFF, "Australia" },
    { 0x800000, 0x83FFFF, "India" },
    { 0x840000, 0x87FFFF, "Japan" },
    { 0x880000, 0x887FFF, "Thailand" },
    { 0x888000, 0x88FFFF, "Vietnam" },
    { 0x890000, 0x890FFF, "Yemen" },
    { 0x894000, 0x894FFF, "Bahrain" },
    { 0x895000, 0x8953FF, "Brunei" },
    { 0x896000, 0x896FFF, "United Arab Emirates" },
    { 0x897000, 0x8973FF, "Solomon Islands" },
    { 0x898000, 0x898FFF, "Papua New Guinea" },
    { 0x899000, 0x8993FF, "Taiwan" },
    { 0x8A0000, 0x8A7FFF, "Indonesia" },
    { 0x900000, 0x9003FF, "Marshall Islands" },
    { 0x901000, 0x9013FF, "Cook Islands" },
    { 0x902000, 0x9023FF, "Samoa" },
    { 0xA00000, 0xAFFFFF, "United States" },
    { 0xC00000, 0xC3FFFF, "Canada" },
    { 0xC80000, 0xC87FFF, "New Zealand" },
    { 0xC88000, 0xC88FFF, "Fiji" },
    { 0xC8A000, 0xC8A3FF, "Nauru" },
    { 0xC8C000, 0xC8C3FF, "Saint Lucia" },
    { 0xC8D000, 0xC8D3FF, "Tonga" },
    { 0xC8E000, 0xC8E3FF, "Kiribati" },
    { 0xC90000, 0xC903FF, "Vanuatu" },
    { 0xE00000, 0xE3FFFF, "Argentina" },
    { 0xE40000, 0xE7FFFF, "Brazil" },
    { 0xE80000, 0xE80FFF, "Chile" },
    { 0xE84000, 0xE84FFF, "Ecuador" },
    { 0xE88000, 0xE88FFF, "Paraguay" },
    { 0xE8C000, 0xE8CFFF, "Peru" },
    { 0xE90000, 0xE90FFF, "Uruguay" },
    { 0xE94000, 0xE94FFF, "Bolivia" },
};

constexpr size_t BLOCK_COUNT = sizeof(BLOCKS) / sizeof(BLOCKS[0]);

} // namespace

namespace IcaoCountry {

const char* Lookup(uint32_t a)
{
    // Binary search rather than SpecialAircraft's linear scan: that table is ~30
    // entries and runs only for the military flag, this one is ~190 and runs for
    // every new contact. Eight comparisons instead of up to 190, on a chip where
    // the ingest loop shares a core with the renderer.
    size_t lo = 0, hi = BLOCK_COUNT;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (a < BLOCKS[mid].lo)       hi = mid;
        else if (a > BLOCKS[mid].hi)  lo = mid + 1;
        else                          return BLOCKS[mid].name;
    }
    return ""; // unallocated gap -- see the note on the table
}

const char* Lookup(const String& hex)
{
    if (hex.isEmpty())
        return "";

    // Skip leading non-hex characters ('~' on local-receiver TIS-B addresses,
    // stray whitespace), matching SpecialAircraft::IsMilitary's tolerance.
    const char* p = hex.c_str();
    while (*p && !std::isxdigit(static_cast<unsigned char>(*p)))
        ++p;
    if (!*p)
        return "";

    return Lookup(static_cast<uint32_t>(std::strtoul(p, nullptr, 16)));
}

} // namespace IcaoCountry
