# Basemap packages: sources, credits and terms

This page covers the map packages served at `/api/v1/blipscope/basemap`. Each package's PROVENANCE section names the source files it was built from, and carries the credit lines below. The format is set out in [basemap-package-format.md](basemap-package-format.md), and the route in [basemap-route.md](basemap-route.md).

## Public domain: works of the United States government

- **TIGER/Line 2024**, U.S. Census Bureau. Water areas (`AREAWATER`), rivers (`LINEARWATER`) and roads (`PRISECROADS`), plus the state and county files used to choose them.
- **The 2025 Census Gazetteer**, places. U.S. Census Bureau.
- **SUB-EST2024**, the Census Bureau's population estimates for places.
- **USGS GNIS Domestic Names**, summits.

## Terrain: AWS Terrain Tiles (Terrarium, zoom 9)

Summit elevations, and the hillshade when a package asks for terrain, come from the AWS Terrain Tiles. Their sources at zoom 9 are SRTM on US land, GMTED2010 above 60° N, ETOPO1 for the ocean, and NRCAN in Canada.

A package carries one `credit.terrain` line for each source its tiles contain. The texts are verbatim from tilezen/joerd's `docs/attribution.md` at `0b86765`. "ETOPO1: " is this route's prefix, and the NRCAN line adds the licence's link.

- SRTM data courtesy of the U.S. Geological Survey
  - **SRTM's notice**, from SRTM's specification §3.15 b as joerd quotes it, with the year of production inserted: "© COPYRIGHT 2000 BY THE UNITED STATES GOVERNMENT. NO COPYRIGHT CLAIMED UNDER TITLE 17 U.S.C."
  - 2000 is the SRTM release joerd names. joerd records several updates, including in 2014.
- GMTED2010 data courtesy of the U.S. Geological Survey
- ETOPO1: DOC/NOAA/NESDIS/NCEI > National Centers for Environmental Information, NESDIS, NOAA, U.S. Department of Commerce
- Contains information licensed under the Open Government Licence – Canada. https://open.canada.ca/en/open-government-licence-canada

## What was done to the elevations

A package carries the `processing.terrain` line that matches what its tiles were read for. GMTED2010's terms ask for a description of modifications, and this line is that description.

- **With terrain levels only:** Terrarium z9 elevations; hillshade from 315 deg at 45 deg with 2x vertical exaggeration; 4x4 mean of z9 pixels; each level pixel takes the z9 pixel containing its centre; shade quantised to 8 bits
- **With summits only:** Terrarium z9 elevations; summit elevations taken from the z9 pixel containing the summit, rounded to whole metres
- **With both:** Terrarium z9 elevations; hillshade from 315 deg at 45 deg with 2x vertical exaggeration; 4x4 mean of z9 pixels; each level pixel takes the z9 pixel containing its centre; shade quantised to 8 bits; summit elevations taken from the z9 pixel containing the summit, rounded to whole metres

## Coverage

Packages cover the 50 states and DC. Outside them, the route answers `no_coverage` rather than an empty package.
