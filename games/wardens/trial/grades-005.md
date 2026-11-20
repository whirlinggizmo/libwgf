# Wardens, libwgf.005: grades

| # | item | grade | seen |
|---|---|---|---|
| 1 | Window 1280 x 720, resizable; arena framed, HUD at edges | met | Main.hx:230-232 size and resizable; W/H read each frame (1738) and HUD laid out from them; fov widened for narrower aspects (1456-1461) |
| 2 | Title: name, best, Play and Quit, warden idling and turning | met | title.png: "WARDENS", "Best: 0:00" (title-crowd.png "Best: 0:10" after a run), skinned Play/Quit, warden whole and idling with the sword in hand; yaw += 0.6*dt (1767) |
| 3 | Play goes fullscreen; Quit ends | met | startPlay calls Window.requestFullscreen (548-549); Quit calls App.quit (1561-1563) |
| 4 | Pause: Escape or P; Resume, Restart, Title, volume 0-100% saved with the best | met | pause.png shows all four; keys at 1778; writeSave writes best and volume together (436-438), on slider release (1525-1527) |
| 5 | Game over: time, level, defeated, new best, Restart and Title | met | late.png: "Time survived: 0:10", "Level 1", "Enemies defeated: 2", "New best!", Restart and Title; drawOver 1597-1618 |
| 6 | Ground 40 x 40 with grid, low walls nothing passes | met | title.png grid and low wall row; Draw.grid(40,1) (1715); wall models scaled to 0.32 high at +-20.5 (352-371); warden, enemies and bullets clamped to the arena (1097-1099, 1274-1276, 1329) |
| 7 | Camera 55 deg from 14 m, smooth follow; sun and fill | met | PITCH 55 deg, CAM_DIST 14, exponential follow (1467-1471); play.png top-down framing; directional sun with shadows and a fill from straight above (289-299) |
| 8 | Warden from four Adventurer parts on one skeleton, animated together | met | Rig.makePart(0, s) for the four slots with Animations.glb clips (310-314, Rig.hx:67-72); play.png and move.png show her whole and coherent |
| 9 | Moving: WASD, arrows, stick; 5 m/s by dt, no faster diagonally; faces movement | met | moveInput 631-681 normalizes above 1; px += inp*speed*dt (1095); yaw turns to atan2 of input (1100-1103); move.png shows her running right, facing right |
| 10 | Touch stick where a finger lands on the play area, stops on lift | met | touchInput 693-751: stick origin at the press point unless on a button, cleared on release/up |
| 11 | Touch buttons: pause top right, switch bottom right with both; neither moves her; menus tapped | met | pauseButtonRect/switchButtonRect (683-687), onBtn excludes them from the stick (710-720); menus read the mouse, which libwgf's first finger drives by default |
| 12 | Animations: Idle, Run, attack, HitRecieve, Death | met | title.png idle, move.png run, play.png standing slash; clips found by name (Rig.hx:46-65); HitRecieve and Death on hurt/kill (1191-1202, 946-949, 990-994) |
| 13 | Attack from the waist up while moving, legs running on | met | startAttackAnim plays a layer from "Abdomen" while moving, base Run continuing (1080-1082, Rig.hx:111-117); move.png shows her running with the sword held |
| 14 | Changes blend over 0.2 s | wrong | base changes use 0.2 (1113, 1121, 1129), but the attack fades in over 0.1 (1082, 1086), and enemies' HitRecieve and Death over 0.1 (993, 948) |
| 15 | Materials as authored, metallic and roughness honoured | met | the parts' glTF materials are never overridden for the warden; title.png shows distinct skin, cloth, leather and metal |
| 16 | Weapon in her right hand through every animation | met | Sword.glb is a skinned model on the same clips, synced to the body (315-322, 1147-1153); title.png, play.png, move.png all show the sword gripped in her right hand |
| 17 | Sword: 0.8 s, 2 m, 120 deg, 10 damage | met | strike 998-1016: swordT 0.8*cdMul, d <= swordReach (2), cos >= 0.5, swordDmg 10 |
| 18 | Gun: 0.5 s, nearest within 12 m in 90 deg, sphere at 20 m/s, first hit, 6 damage | met | fireGun 1018-1070 (12 m, cos >= 0.7071, sorted nearest, 0.5*cdMul); updateBullets 20*dt, swept for the first enemy, 6 damage (1295-1335); unlit bright sphere |
| 19 | Both weapons: last picked held, Q or shoulder switches | met | picking the gun holds it (1430-1434); Q, either bumper or the touch button switch (1133-1135) |
| 20 | Health 100, bar above her head facing the camera | met | play.png and move.png: green bar above her head; projected from her head each frame (1687-1693) |
| 21 | Enemies: other outfits' parts, tinted red, running | met | crowd.png and pause.png: whole bodies of the other outfits (witch hats, soldiers, sci-fi), red-tinted, in Run; outfits 1-9 (919), tint (822) |
| 22 | Spawning at edge, out of view; 2.5 m/s at her; pushing apart | met | trySpawn 843-901 picks edge points and prefers ones off-screen; 2.5*dt toward her (1217-1221); pair push under 0.8 m (1242-1269); crowd.png converging from all sides |
| 23 | Contact 8/s; health 20 +5 a minute; HitRecieve, Death; sinking | met | 8*dt per touching enemy (1222-1223); hp 20 + 5*floor(t/60) (786); hit/death clips; sinks after 1.1 s and recycled (1279-1288) |
| 24 | Waves: 5, +10 every 30 s, up to 200 | met | enemyCap min(200, 5 + 10*floor(t/30)) (763-767) |
| 25 | Gems: dropped, small green spinning crystal, pulled within 3 m, 1 xp | met | killEnemy drops a gem (955-977), a 4-sided stretched sphere (diamond) in unlit green spinning (1362-1364); pull 3 m, xp++ (1339-1360); play-later.png xp bar filled |
| 26 | Death bursts: 40 red sparks, falling, additive, 0.6 s fade; up to 2,000 | met | 24 emitters, max 84 each (2,016), life 0.6, gravity, red fading to alpha 0, AlphaMode.ADD (326-338); burst(40) (954) |
| 27 | Target ring: thin, sword reach, pulsing once a second, own shader on ground coords | met | shaders/ring.glsl uses wgf_world_pos.xz and sin(time*2pi); radius = swordReach (1827-1829); play.png thin cyan ring around her |
| 28 | Spawn warnings: red circle filling over 1 s, own shader with fill passed in | met | shaders/warn.glsl fills to radius*area.w; fill min(1, w.t) passed each frame, enemy spawned at 1 s (928-938) |
| 29 | Hit flash: white for 0.1 s, still animating, own shader on animated lit enemy | met | shaders/flash.glsl lights the surface and mixes toward white; matFlash swapped onto the enemy's skinned models for 0.1 s (828-841, 988, 1233-1237) |
| 30 | Experience bar along the top; level 2 at 5, each +5 | met | play-later.png green bar across the top with "1" at its left; need = level*5 (1367-1371) |
| 31 | Level up: play stops, three cards with name, icon, text; click, tap, arrows and Enter, gamepad | met | openChoice pauses the world (1394-1422); drawChoose 1620-1653 draws icon, name and text and picks by mouse/touch or nav (arrows, Enter, d-pad, stick, south); no shot shows one |
| 32 | The cards as the spec gives them | met | pick 1424-1451: +5 damage, cdMul*0.85 for every weapon, reach +0.5, gun once (only offered without it), shots++ |
| 33 | Gear: random part of an outfit she isn't wearing, swapped at once without a jump, its bonus | met | randGear picks an outfit other than the slot's (1386-1392), named "Witch Head" style; replacePartNow syncs the new part to the old before swapping (534-538, 1164-1178); bonuses +10 hp, armor*0.9, +0.5 speed, +1 pull |
| 34 | Cards and buttons from panel.png and button.png, nine-slices | met | Draw.textureNineSlice with 16/14 px borders (1481-1494); title.png and pause.png show unstretched corners |
| 35 | HUD: xp bar and level, time, defeated and fps, gear and weapon icons; none turns | met | play.png: top bar with "1", "0:03" top left, "Defeated: 0" and "71 fps" top right, four gear icons and the sword icon at the bottom, all screen-space |
| 36 | Sound: five effects, music looping, ducked to half when paused, all following volume | met | slash, shot, hit, gem, level played (1000, 1068, 1186, 1358, 1421); music looping (280), 0.5 on pause (575); Audio.setVolume(volume) global |
| 37 | Run options --enemies and --seed; same seed same enemies | met | App.getOptionInt("enemies"/"seed") (252-255); spawn and outfits from Random.create(optSeed) (524); crowd.png ran with 200 |

Counts: met 36, wrong 1, missing 0, can't tell 0.
