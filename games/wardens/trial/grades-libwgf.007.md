# Wardens, libwgf.007: grades

| # | item | grade | seen |
|---|---|---|---|
| 1 | Window 1280 x 720, resizable; arena framed, HUD at edges | met | Main.hx:388-390 size, title and resizable; updateCamera 1539-1540 pulls back for windows narrower than 16:9; HUD placed from logicalW/H (1707-1756). |
| 2 | Title: name, best, Play and Quit, warden idling and turning | met | title.png: "Wardens", "Best: 0:09", skinned Play and Quit buttons, the warden whole in the Idle pose with the sword's pommel in her right hand; turns at 0.7 rad/s (1924-1925). |
| 3 | Play goes fullscreen; Quit ends | met | Main.hx:1811-1816: Window.requestFullscreen(true) then startGame; App.quit. |
| 4 | Pause: Escape or P, Resume/Restart/Title, volume 0-100% saved with best | met | pause.png shows Paused, Resume, Restart, Title, "Volume 80%" and slider; keys at 1915; save() writes best and volume to one file (819), on resume and Title. |
| 5 | Game over: time, level, defeated, new best, Restart and Title | wrong | late.png has the panel and buttons, but `time += dt` runs on through the 1.8 s death (1929, 1937), so it shows "Time survived: 0:10" against a best of 0:09 with no new best called out; the best is recorded at death (1465), not the time shown. |
| 6 | Ground 40 x 40 with grid, low walls nothing passes | met | 40 m plane with ground.glsl's 1 m and 5 m grid (measured 61 px a meter in play.png); wall.glb at 0.35 height around ±20.5 (536-546); warden clamped to ±19.5 (1482-1484), enemies to ±19.6 (1240-1242). |
| 7 | Camera 55 degrees, 14 m, smooth follow; sun and fill | met | updateCamera 1535-1543: exponential follow, 14 m at 55 degrees; directional sun and a fill from above (441-450); play shots match. |
| 8 | Warden from four Adventurer parts, one skeleton, animated together | met | buildWarden 598-603 makes the four Adventurer parts, each played the same clip by Rig; title.png and play.png show her whole and in step. |
| 9 | WASD, arrows, stick; 5 m/s whatever the frame rate; no faster diagonally; faces movement | wrong | Input normalised (880-884) and she faces movement (1480; move.png), but dt is clamped to 0.05 (1905), so below 20 fps she moves slower than 5 m/s; crowd.png's clock reads 0:09 twenty seconds after the click. |
| 10 | Touch stick where a finger lands, moves as left stick, stops on lift | met | readTouch 914-958: stick set on a press in play outside the buttons, scaled to 70 px and clamped to 1, cleared on release. |
| 11 | Touch buttons: pause top right, switch bottom right with both; no movement; menus tapped | met | pauseButtonRect and switchButtonRect (895-899), drawn once touched (1741-1750); a press on either returns or continues before the stick (916-922); cards tapped (1663-1673), menus through Ui. |
| 12 | Animations: Idle, Run, attack, HitRecieve, Death | met | Idle on title.png, Run and Sword_Slash in move.png and play.png, enemies running in pause.png and crowd.png; HitRecieve and Death in hurt and updateWarden (1121, 1127, 1464, 1513). |
| 13 | Attack from the waist up while moving | met | startAttack 1316-1319 plays the clip on a layer from the Abdomen bone while moving; move.png shows her legs running and arms raising the sword. |
| 14 | Blends over 0.2 s | wrong | The warden's main changes blend over 0.2 (1521), but the moving attack fades in over 0.1 (1318) and the enemies' HitRecieve and Death over 0.1 (1121, 1127). |
| 15 | Materials as authored, metallic and roughness honoured | met | Parts keep their glTF materials (no override except the flash); the shots show the varied cloth, skin and metal of the Adventurer parts. |
| 16 | Weapon in her right hand through every animation | met | Sword.glb and Gun.glb are played on the same rig (604-610), so they follow Middle1.R; the sword is in her hand in title.png, play.png and move.png. |
| 17 | Sword 0.8 s, 2 m, 120 degree arc, 10 damage | met | updateWeapons 1331-1348: 0.8 times atkMul, reach 2, cos(60 degrees) half-arc, swordDmg 10. |
| 18 | Gun 0.5 s, nearest within 12 m in 90 degrees, glowing sphere 20 m/s, first hit, 6 damage | met | nearestTargets(12) with cos(45 degrees) (1305), 0.5 s (1353), a 0.17 m bright yellow sphere moving 20 m/s in 0.3 m steps, stopping at the first hit for 6 (1385-1406). |
| 19 | Both weapons: the one picked last held; Q or shoulder switches | met | Picking the gun holds it (1597-1600); Q and either bumper switch (1918-1920). |
| 20 | Health 100, a bar above her head facing the camera | met | hp 100; a world-space bar 2.5 m over her, tilted to the camera's pitch (626, 1888); seen in play.png and move.png. |
| 21 | Enemies: other outfits' parts, tinted red, running | met | Merged meshes of outfits 1-9 and mixes (667-676), tint 255,100,100; move.png, pause.png and crowd.png show whole red figures running. |
| 22 | Spawn at edge, out of view where possible; 2.5 m/s at her; pushing apart | met | edgePoint on the edges, retried while in view (1037-1042); 2.5 m/s straight at her (1175-1176); grid push-apart at 0.8 m (1186-1225); crowd.png shows a spread ring of them. |
| 23 | Contact 8/s; health 20 +5 a minute; HitRecieve, Death, sinking | met | 8 times armor per touching enemy per second (1245); hpNow 20 + 5 a minute (1011); HitRecieve and Death (1121, 1127); sink then release (1268-1281). |
| 24 | Waves 5, +10 every 30 s, up to 200 | met | cap = min(200, 5 + 10 * floor(time / 30)) (1020). |
| 25 | Gems: small green spinning crystal, pulled within 3 m, 1 experience | met | dropGem on a kill; low-poly sphere stretched 1.6 tall, green emissive, spinning (645-650, 1439); pulled within pull = 3 (697, 1426); gainXp adds 1. play-later.png's bar shows 2 of 5. |
| 26 | Death bursts: 40 red sparks, flying and falling, added, 0.6 s, up to 2,000 | met | 20 emitters of 100 (2,000), burst(40), additive, life 0.6, red fading to alpha 0, upward spread with gravity (630-643, 1133-1138). |
| 27 | Target ring: thin, sword's reach, pulsing once a second, own shader on ground coords | met | ring.glsl works from wgf_world_pos.xz, radius = reach (1898), sin(time * 2 pi); play.png measures radius 2.0 m, about 0.13 m wide. |
| 28 | Spawn warnings: red circle filling from middle over 1 s, own shader, fill passed in | met | warn.glsl fills to radius times info.w; the game passes min(1, t) (1026) and spawns at t >= 1 when the edge point is in view (1044-1065). |
| 29 | Hit flash: white 0.1 s, still animating, own shader on the lit, animated enemy | met | flash.glsl lights the surface and mixes toward white by info.x; set on the skinned enemy's models for flashT 0.1 (1095-1104, 1128, 1262-1265) while HitRecieve plays. |
| 30 | XP bar along the top; level 2 at 5, each 5 more | met | drawHud 1711-1715; need 5 * level (1446); play-later.png shows the bar at 40%. |
| 31 | Level up: play stops, three cards with name, icon, text; click, tap, arrows+Enter, gamepad | met | world paused and Mode.Level (1941-1945); drawCards draws name, 96 px icon and text; updateCards takes arrows, Enter, mouse, touch, d-pad, stick and South. No shot shows one. |
| 32 | The cards as the spec gives them | met | pickCard 1589-1602: +5 damage, times 0.85, +0.5 reach, the gun offered only without it, More bullets adds a shot at the next nearest (nearestTargets(12, shots)). |
| 33 | Gear: part of an outfit she isn't wearing, swapped at once without a jump, its bonus | met | gearCard picks outfits not in wOutfit (1573), named "Witch Head" style with the outfit icon; setPart syncs the new part to the playing clip (138, 720-731); +10 max hp, armor times 0.9, +0.5 m/s, +1 m pull (1605-1615). |
| 34 | Cards and buttons from panel.png and button.png as nine-slices | met | Ui skins with 16 and 14 px borders (510-518), cards by Draw.textureNineSlice (1781); title.png and pause.png show the wooden art with unstretched corners. |
| 35 | HUD: XP bar and level, time, defeated and fps, gear and weapon icons; screen-fixed | met | play.png: "Lv 1" bar, 0:02 top left, "Defeated: 0" and fps top right, four gear icons and the sword icon at the bottom, all drawn in screen space. |
| 36 | Sound: five effects, music looping, ducked to half paused, volume | met | slash (1333), shot (1354), hit (1249), gem (1444), level (1450); streamed music looping (773-779), 0.5 when paused (970); Audio.setVolume from the slider. |
| 37 | --enemies=N and --seed=N; same seed plays the same enemies | wrong | Both read (416-421) and spawns use a seeded generator, but 8 of the 17 enemy kinds are mixes picked with the unseeded Std.random (673), so the same seed gives different enemies. |

Met 33, wrong 4, missing 0, can't tell 0.
