# Blipscope enclosure (V5)

The printed case for Blipscope: the same parts we print for the assembled units.

```
enclosure-v5/
  step/   source geometry, one file per part (re-slice or remix)
  3mf/    Bambu Studio projects with our print settings, one per plate
```

STL files are on Printables and MakerWorld for anyone who needs them.

## Parts

| Part | Plate | Colour |
|---|---|---|
| bezel (instrument ring) | 1 | black |
| bottom-housing (weighted base) | 1 | black |
| back-small (USB port block) | 1 | black |
| front-housing | 2 | body colour |
| back-plate | 2 | body colour |

Assembled units are printed in Polymaker Panchroma PLA Matte: Ash Grey body, black bezel and base.

## Print settings

- 0.4 mm nozzle, 0.20 mm layers
- 2 walls, 15% gyroid infill
- No supports; every part is oriented to print support-free
- Auto brim
- PLA Matte

The 3MF projects are set up for a Bambu Lab P2S. Any printer with a 180 mm bed works; import the STEP files and use the settings above.

## Hardware

Everything below comes in the [Blipscope DIY Kit](https://valarsystems.com/products/blipscope). The only thing to buy separately is the printed case.

- Flashed ESP32-S3 board with 1.28" round touch display
- 2.4 GHz FPC antenna
- USB-C panel-mount extension, 10 cm
- 4 × M2 screws (board)
- 10 × M2 × 10 mm flat-head thread-forming screws (bezel and housing)
- 2 × M2 pan-head screws + 2 × M2 hex nuts (USB panel mount)
- 4 × steel weights, 16 × 19 × 3 mm (base)
- 4 × non-slip pads

## Assembly

The step-by-step build guide is on the support page, so there is one copy of the steps: **<https://scopes.valarsystems.com/blipscope/support#kit>**. The QR code on the card in the kit box goes there too.

Tools: a T6 Torx driver.

## License

The enclosure files in this folder are licensed under [CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/): print them, modify them and share remixes with credit to Valar Systems, but don't sell prints. This matches the MakerWorld and Printables listings. The firmware in the rest of the repository is under the Open Community License (OCL v1); see [LICENSE](../LICENSE).
