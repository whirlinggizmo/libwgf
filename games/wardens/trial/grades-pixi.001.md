# Wardens, pixi.001: grades

| # | item | grade | seen |
|---|---|---|---|
| 1 | Window 1280 x 720, resizable; arena framed, HUD at edges | met | main.js:12 `resizeTo: window`; game.js:495-496 widens the fov for narrow aspects; main.js:49 relayouts the HUD on resize, ui.js:92-103 places it from the window's size |
| 2 | Title: "Wardens", best time, Play and Quit, warden idling and turning | wrong | title.png shows play already running at 0:03 with the HUD, no title: main.js:171 starts a run whenever `enemies` or `seed` is given; the title is in the code (ui.js:120-125, game.js:392) but never shown in the graded run |
| 3 | Play goes fullscreen; Quit ends the game | met | main.js:33 `requestFullscreen` on Play; main.js:34 Quit closes the window, stops the ticker and clears the stage |
| 4 | Pause on Escape or P; Resume, Restart, Title, volume slider 0-100%, saved with the best | met | pause.png shows all four with "Volume: 70%"; main.js:92 Escape/KeyP; main.js:38 and data.js save volume in the same record as best |
| 5 | Game over: time, level, defeated, new best, Restart and Title | met | late.png: "Time survived: 0:12", "Level 1", "Enemies defeated: 2", "New best!", Restart and Title; ui.js:139-147 |
| 6 | Ground 40 x 40 with a grid, low walls nothing passes | met | grid drawn by shaders.js:360-383 inside ARENA=20 (game.js:58); low wall models at +-20.5 (game.js:62-68); warden and enemies clamped inside (game.js:405-406, 470) |
| 7 | Camera about 55 degrees from 14 m, following smoothly; sun and fill | met | game.js:488 pitch 55, d 14, eased focus (493-494); DirectionalLight sun game.js:51 and a hemisphere sky/ground fill main.js:15; play shots match |
| 8 | Warden from the four Adventurer parts on one skeleton, animated together | met | game.js:152 sets the four Adventurer parts; rig.js:251-259 Body's player poses, the others copy its skeleton; play.png/move.png show her whole and in one pose |
| 9 | WASD, arrows, stick; 5 m/s; no faster diagonally; faces movement | met | main.js:151-152 keys + pad + touch; game.js:400-409 normalizes over length 1, moves at speed*dt, turns to atan2(ix,iz); move.png shows her facing right with D held |
| 10 | Touch stick where a finger lands on the play area; stops on lift | met | main.js:106-118: stick placed at the touch point only when the target is the stage, knob vector added like the pad's, cleared on pointerup/cancel |
| 11 | Touch pause top right, switch bottom right once both; neither moves her; menus tapped | met | ui.js:189-195 pause at (w-92, 84), swap at (w-116, H-130), shown only with both weapons (ui.js:194); buttons stop propagation (ui.js:33) and the stick needs target===stage; buttons, slider and cards use pointer taps |
| 12 | Animations: Idle, Run, attack, HitRecieve, Death | met | game.js:12, 336, 356, 482-483, 257-259; play/move shots show Run on enemies and a slash pose on her, all bodies intact |
| 13 | Attack from the waist up while moving, legs running on | met | rig.js:191 cuts each clip into upper/lower by LOWER (data.js: Root, Body, Hips, legs, feet); rig.js:243 keeps the lower channel on Run during an attack while moving; move.png shows a slash with legs in stride |
| 14 | Blends over 0.2 s | met | rig.js:196 FADE=0.2; rig.js:244-249 weights ramp by dt/0.2 |
| 15 | Materials as authored, metallic and roughness honoured | met | parts loaded as glTF Model3D with their own materials (game.js:132); the loader reads metallicFactor/roughnessFactor; shots show skin, cloth and leather shaded differently |
| 16 | Weapon in her right hand through every animation | met | Sword.glb/Gun.glb as skinned models driven by the same rig times (game.js:125-128, rig.js:252); play.png and move.png show the sword in her right hand, down at idle and raised in the slash |
| 17 | Sword every 0.8 s, 2 m, 120 degree arc, 10 damage | wrong | game.js:336-343: 0.8 s, cos(60) arc and 10 damage are right, but hits reach `this.reach + 0.3` = 2.3 m (game.js:337), and anything within 0.45 m is hit whatever its direction |
| 18 | Gun every 0.5 s, nearest within 12 m in 90 degrees, glowing sphere at 20 m/s, first hit, 6 damage | met | game.js:346-357 filters 12 m and cos(45), sorts by distance, cd 0.5; fireShot speed 20 (318), emissive sphere (89-90), first enemy touched takes 6 (325-326) |
| 19 | Both: the one picked last held; Q or shoulder switches | met | picking Gun sets held='gun' (game.js:368); Q main.js:93, pad buttons 4/5 main.js:129; swap game.js:170-174 |
| 20 | Health 100, bar above her head facing the camera | met | game.js:157 hp 100; play.png/move.png show a green bar above her, flat to the camera (Sprite3D, game.js:98-102, 426) |
| 21 | Enemies: other outfits' parts, tinted red, running | met | ENEMY_OUTFITS excludes Adventurer (data.js); tint via enemyBit; play.png/move.png show red figures of several outfits running, whole and unstretched |
| 22 | Spawn at the edge, out of view where they can; 2.5 m/s at her; pushing apart | met | game.js:223-233 edge points at 19.2 m, up to 8 tries for one out of view; 444 moves at 2.5 toward her; 448-466 grid separation; crowd.png shows a large, spread-out crowd |
| 23 | Contact 8/s; health 20 +5 a minute; HitRecieve and Death; sinking away | met | game.js:481 8*touching*armor*dt; 167 enemyHp; 257-259 Death/HitRecieve; 439 sinks after 1.3 s and is recycled |
| 24 | Waves: 5, +10 every 30 s, up to 200 | met | game.js:166 `min(200, 5 + 10*floor(time/30))` |
| 25 | Gems: dropped, small, green, spinning; pulled within 3 m; 1 xp | met | game.js:289-309: green emissive icosahedron r 0.2, spins, pull 3, gainXp +1; none caught in a shot |
| 26 | Death bursts: 40 red sparks, flying and falling, additive, fading over 0.6 s; up to 2,000 | met | game.js:263-287: 40 per burst, outward and up velocity, gravity 14, life 0.6 with alpha fading, red/orange-red colors; ParticleContainer3D capacity 2000, blendMode 'add' (game.js:92-94) |
| 27 | Target ring: thin, sword's reach, pulsing once a second, own shader on ground coordinates | met | shaders.js:386-407 ring from vPosition.xz minus center, width 0.05-0.08, sin(uTime*2pi); radius = this.reach (game.js:425); play.png shows a thin cyan ring about 4 m across |
| 28 | Spawn warnings: red circle filling from the middle over 1 s, own shader, fill passed in | met | shaders.js:410-431 fill radius = fill*radius; game.js:236-239 fill = t over 1 s, then the enemy spawns; none caught in a shot |
| 29 | Hit flash: white for 0.1 s, still animating, own shader on the lit enemy | met | shaders.js:346-357 mixes finalColor to white by `flash`; game.js:259 flash 0.1 swaps to the flash materials, 436 swaps back; the rig keeps updating |
| 30 | Experience bar along the top; level 2 at 5, each 5 more | met | ui.js:94,104 full-width bar at the top (play-later.png shows it part filled); game.js:168 xpNeed = 5*level |
| 31 | Level up: play stops, three cards; click, tap, arrows and Enter, gamepad | met | crowd.png shows "Level up!" with three cards, each icon, name, text; main.js:149 freezes play; ui.js:167-168, 180-181, main.js:85, 144-146 |
| 32 | The cards as the spec gives them | met | game.js:364-369: Sharper +5, Quick cdMult*0.85 (both weapons use it), Long reach +0.5, Gun only until held, More bullets one more at the next nearest |
| 33 | Gear: random part of an unworn outfit, swapped at once without a jump, its bonus | met | game.js:370-376 outfits not worn, named "Suit Legs" (crowd.png); setPart swaps the model, rig clip times are shared so no jump (rig.js:226, 234-259); bonuses +10 hp, armor*0.9, +0.5 m/s, +1 pull |
| 34 | Cards and buttons from panel.png and button.png as nine-slices | met | ui.js:20-27 NineSliceSprite for panel and buttons; pause.png and crowd.png show the panel and buttons with unstretched corners |
| 35 | HUD: bar and level, time, defeated and FPS, gear and weapon icons; screen-fixed | met | play.png: bar on top with "Lv 1" at its left end, time top left, "Defeated" and "FPS" top right, four gear icons and the sword icon at the bottom; HUD on the 2D stage |
| 36 | Sound: five effects, music looping, ducked to half when paused, following volume | met | audio.js loads all six; play calls at game.js:336, 356, 482, 309 and main.js:39/159; music loop audio.js:338; duck 0.5 main.js:59; all through the master gain set by volume |
| 37 | `--enemies=N` and `--seed=N`; same seed, same enemies | met | main.js:8-9 reads both (the runs took them: 10 and 200 enemies seen); game.js:154 seeds mulberry32, which drives spawn points and outfits |

Met 35, wrong 2, missing 0, can't tell 0.
