# D'oh!

This is a port of The Simpsons Hit & Run to the Nintendo Switch and PS Vita based on the leaked source code. The full game should be playable, including local multiplayer in the bonus game. The port is however still incomplete, so some glitches can be observed and some visual effects are missing compared to the PC version.

Please report any bugs or feature requests in the issues tab on this Github repository.

# Installation

This port uses the PC assets, so you will need to have the PC version of the game installed. Do not use the assets from the source code leak as those are not the final version, instead use the assets from the official release. Also make sure you're using the original `.rmv` movie files in the `movies` folder rather than the converted `.bk2` files that older releases of the port required.

To install the port simply copy the contents of the installation folder to `sdmc:/switch/simpsons` on the Switch or `ux0:/data/simpsons` on the Vita.

Finally download the [latest release](https://github.com/ZenoArrows/The-Simpsons-Hit-and-Run/releases) of this port and copy it to your console. On the Switch you simply put the `.nro` file into the same folder as the game data (`sdmc:/switch/simpsons`), on the Vita you can copy the `.vpk` file anywhere and install it using [VitaShell](https://github.com/TheOfficialFloW/VitaShell).

On the PS Vita this game also requires that you have `libshacccg.suprx` installed on your console. This will be installed during the first run setup of the [VitaDB Downloader](https://vitadb.rinnegatamante.it/#/info/877), but can also be installed separately using [ShaRKBR33D](https://vitadb.rinnegatamante.it/#/info/997).

# Multi-Language support

The PAL version supports multiple languages and will use the language that matches the system language of your console. If your console is set to a language that is not supported a menu will be shown giving you the option to choose between the supported languages.

No official release has the dialog RCF files for all 4 supported languages, so you will need to make sure you use the game assets from a release that's localized in the language you'd like to play.

If you'd just like to play in English and have no need for multi-language support, then use the NTSC version to play.
