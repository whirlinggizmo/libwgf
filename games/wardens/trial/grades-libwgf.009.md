# Wardens, libwgf.009: grades

| # | item | grade | seen |
|---|---|---|---|
| 1 | Window 1280 x 720, resizable; arena framed, HUD at edges | met | Main.hx:225-227 sets 1280x720, resizable, Presentation EXPAND; updateCamera widens the fov for narrow windows (1690-1696); HUD placed from Presentation.getVisible (1546-1550, 1880-1890) |
| 2 | Title: "Wardens", best, Play and Quit, warden idling and turning | met | title.png: "WARDENS", "Best: 0:08", skinned Play and Quit, the warden whole and idling with the sword in her hand; turns 0.6 rad/s (1567) |
| 3 | Play goes fullscreen; Quit ends the game | met | Main.hx:1772-1779: Window.requestFullscreen(true) then startGame; Quit calls App.quit |
| 4 | Pause on Escape or P: Resume, Restart, Title, volume slider, saved with best | wrong | updatePlay sets PAUSE on Escape/P (1618), then in the same frame the PAUSE screen reads Keyboard.isPressed(ESCAPE/P) as Resume (1789) and resumes at once; pause.png shows play running (time 0:05, enemies running), no panel. Panel, slider and save (682, 1786-1805) are written |
| 5 | Game over: time, level, defeated, new best, Restart and Title | met | play-later.png and late.png: Time survived 0:10, Level 1, Enemies defeated 2, "New best!", Restart and Title; crowd.png shows "Best: 0:10" when not a best |
| 6 | Ground 40 x 40 with a grid, low walls nothing passes | met | 40x40 plane and wall.glb pieces at +-20.5 scaled 0.4 high (447-474); Draw.grid 20 x 2 m (1702); warden and enemies clamped to the arena (1643-1644, 1153-1154); walls and grid in title.png, pause.png |
| 7 | Camera about 55 degrees from 14 m, following smoothly; sun and fill | met | updateCamera: 55 degree elevation, distance 14, exponential follow (1679-1689); directional sun and a downward fill light (292-301); move.png shows the lag |
| 8 | Warden from four Adventurer parts on one skeleton, animated together | met | four Adventurer_*.glb models under wRoot, each given Animations.glb, played together by rigPlay (309-315, 523-530); she is whole in title.png and play.png |
| 9 | WASD, arrows, left stick; 5 m/s frame-rate free, not faster diagonally; faces movement | met | moveInput (1426-1460) normalizes to length 1; speed 5 * dt (1637-1640); facing = atan2 (1641); move.png shows her moved right |
| 10 | Touch stick where a finger lands, as the left stick, stops on lift | met | touchInput (1486-1537): stick origin at the press on the play area, scaled to 60 px, added as stick input; cleared when the finger lifts |
| 11 | Touch pause top right, switch bottom right once both; neither moves her; menus tapped | met | pause/switch rects at the top and bottom right (1465-1477); those fingers are kept from the stick (1501-1507); menus use the mouse, which the finger drives (Touch.setMouseEmulated, 243) |
| 12 | Animations: Idle, Run, attack, HitRecieve, Death | met | clips resolved by "CharacterArmature\|" names (509-510); Idle/Run, Sword_Slash/Gun_Shoot, HitRecieve on contact, Death when dead (566-605); title.png idling, play shots running and slashing, bodies posed correctly |
| 13 | Attack from the waist up while moving, legs running on | met | while moving the attack plays as a layer from the "Abdomen" bone (a bone in the parts) over Run (536, 597-599); move.png shows the run with a swing |
| 14 | Blends over 0.2 s | met | rigPlay and rigLayer fade 0.2 (576, 598, 603); only a layer stopped for a standing attack fades 0.1 (593) |
| 15 | Materials as authored, metallic and roughness honoured | met | the warden's parts keep their glTF materials; enemy shader takes each part's base color and roughness (942, enemy.glsl); title.png shows skin, cloth and leather distinct |
| 16 | Weapon in her right hand through every animation | met | Sword.glb and Gun.glb share the clips and play with the rig (316-322, 499-500); the sword is in her right hand in title.png, play.png, move.png |
| 17 | Sword every 0.8 s, 2 m, 120 degrees, 10 damage | met | interval(0.8) (1308); reach 2.0 to an enemy's edge, cos(60 degrees) half-arc, swordDmg 10 (1358-1376) |
| 18 | Gun every 0.5 s, nearest within 12 m in 90 degrees, glowing sphere 20 m/s, first hit, 6 damage | met | fire(): 12 m, cos(45 degrees), sorted nearest (1378-1394); emissive unlit sphere r 0.14 (380-383); 20 * dt, first enemy hit for 6 (1324-1345) |
| 19 | The one picked last held; Q or shoulder switches | met | picking the gun holds it (862-865); Q, LB or RB call switchWeapon (1625-1626) |
| 20 | Health 100, bar above her head facing the camera | met | hp 100 (717); bar at y 2.35 turned to the camera's rotation (1716-1721); green bar above her in play.png, move.png |
| 21 | Enemies: other outfits' parts, tinted red, running | met | outfits 1-9 merged from their four parts (326-335), tint (255,85,85), Run (1105-1110); red running figures in pause.png and crowd.png |
| 22 | Spawn at the edge, out of view where possible; 2.5 m/s; pushing apart | met | pickSpawn on the edge preferring out of view (984-1020); 2.5 * dt toward her (1119-1120); grid push-apart (1122-1144); crowd.png shows them ringed around her |
| 23 | Contact 8/s; health 20 +5 a minute; HitRecieve, Death; sinking | met | 8 * touching * dt (1186); hp 20 + 5 * floor(t/60) (970); HitRecieve and Death clips (1222-1229); sinks after 1 s and is released at 2 s (1094-1103) |
| 24 | Waves: 5, +10 every 30 s, up to 200 | met | enemyCap: min(200, 5 + 10 * floor(t/30)) (912-916) |
| 25 | Gems: dropped, small, green, spinning; pulled within 3 m; 1 xp | met | dropGem on a kill (1217, 1243-1262); green emissive diamond (374-379, 407-444) spun (1294); pull 3 m, xp++ (1265-1284); the xp bar fills in play-later.png |
| 26 | Death bursts: 40 red sparks, flying and falling, added, 0.6 s; up to 2,000 | met | 50 emitters of 40, life 0.6, gravity, red fading to alpha 0, AlphaMode.ADD (386-399); burst(40) (1234-1240) |
| 27 | Target ring: thin, sword's reach, pulsing once a second, own shader on ground coordinates | met | ring.glsl uses wgf_world_pos.xz and sin(t * 2 pi); radius swordReach passed in (1710); thin ring at her feet in play.png, move.png |
| 28 | Spawn warnings: red circle filling over 1 s, own shader, fill passed in | met | warn.glsl fills to radius * info.w; the game passes w.t, spawning at 1 s (1054-1066) |
| 29 | Hit flash white 0.1 s, still animating, own shader on the lit enemy | met | flash 0.1 sets a white tint (1160-1170, 1225); enemy.glsl lights the skinned surface and mixes to white on the tint (enemy.glsl) |
| 30 | XP bar along the top; 5 gems for level 2, +5 each level | met | bar and "Lv" at the top in play.png; xpNeed = 5 * level (803-804) |
| 31 | Level up: stops, three cards with name, icon, text; click, tap, arrows and Enter, gamepad | met | beginLevelUp pauses the stage, three cards (806-829); cards drawn with icon, name, text (1807-1836); Gui takes mouse, arrows, Enter, pad south and d-pad (1938-2002) |
| 32 | Cards: Sharper blade, Quick hands, Long reach, Gun once, More bullets | met | makeCard and pickCard (831-891): +5 damage, 0.85x interval, +0.5 m, gun only when not held, one more shot at the next nearest (1397) |
| 33 | Gear: random part of an outfit not worn, swapped without a jump, its bonus | met | random slot and a different outfit (843-849); swapPart syncs the new part's animation to the old (615-647); +10 hp, 0.9x damage, +0.5 m/s, +1 m pull (869-880) |
| 34 | Cards and buttons from panel.png and button.png as nine-slices | met | Draw.textureNineSlice for panel and button (1748-1758); title.png and the game over panel show skinned art with corners intact |
| 35 | HUD: xp bar and level, time, defeated and fps, gear and weapon icons; screen-fixed | met | play.png: Lv 1 and the bar, Time 0:02, Defeated 0, 122 fps, four gear icons and the sword icon, drawn in screen space |
| 36 | Sound: five effects, music looping and ducked to half when paused, volume | met | slash, shot, hit, gem, level played (1359, 1395, 1189, 1285, 827); music.ogg looped (279-280), 0.6 in play, 0.3 paused (687); Audio.setVolume(volume) (686) |
| 37 | --enemies=N and --seed=N; same seed, same enemies | met | App.getOptionInt("enemies") holds the cap (249, 913-914); Random.setSeed from seed (244-245), all spawning from Random |

met 36, wrong 1, missing 0, can't tell 0
