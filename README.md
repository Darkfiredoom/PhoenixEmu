# PhoenixEmu case art

Game case scans PhoenixEmu downloads for its Case look (the 3D case that turns and opens) and the
home screen banners. PhoenixEmu reads `manifest.json` and fetches a game's files the first time it
sees the game.

- `Cases/<game>/<game>.jpg`: the whole cover, back | spine | front side by side
- `Cases/<game>/<game>-Inside.jpg`: the inside of the cover
- or `Cases/<game>/<game>-Front.jpg`, `-Back.jpg`, `-Side.jpg`
- `Banner/<game>.jpg`: a wide picture for the home background

`manifest.json` lists each game's folder, its title IDs and names (a game is matched by title ID,
else by name), its files and its banner. To add a game, add its folder and an entry.

Scans gathered from GameFAQs and r/NintendoSwitchBoxArt.
