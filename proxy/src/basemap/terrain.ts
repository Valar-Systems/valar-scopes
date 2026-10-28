// Terrain (docs/basemap-package-format.md §5.4, §5.6; §13's RD10, RD16, RD21, RD27, RD28). Pure.
//
// So far only the Terrarium decode, which the peaks layer needs for RD10 (ML10). The hillshade, the 4 x 4 mean, the
// level sampling and the shade byte land here with the MT family.

/** §5.6: Terrarium's elevation in metres, R*256 + G + B/256 - 32768, in that order, in double. */
export const decodeTerrarium = (r: number, g: number, b: number) => r * 256 + g + b / 256 - 32768;

/** RD10: a summit's elevation, the z9 pixel's Terrarium value then floor(v + 0.5). */
export const elevationM = (r: number, g: number, b: number) => Math.floor(decodeTerrarium(r, g, b) + 0.5);
