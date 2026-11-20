# Wardens (libwgf, run2): grades

| # | item | grade | seen |
|---|---|---|---|
| 1 | Window 1280 x 720, resizable; arena framed, HUD at edges | met | Main.hx:48-52 setResizable, Presentation EXPAND at 1280x720; camera follows her (placeCamera); HUD laid out from Presentation.getVisible edges (hud(), :509-542) |
| 2 | Title: "Wardens", best time, Play and Quit, warden idling and turning | met | title.png: "Wardens", "Best: 0:00", Play/Quit, warden whole and in idle, sword in her right hand; Game.hx:215-223 heading += 0.35/s with Idle |
| 3 | Play goes fullscreen; Quit ends the game | met | Main.hx:236-237 requestFullscreen in startPlay; :425-426 Quit calls App.quit |
| 4 | Pause on Escape or P: Resume, Restart, Title, volume 0-100%, saved with best | met | pause.png: panel with Resume, Restart, Title, "Volume 80%" slider; Main.hx:169-170 Escape and P; Save.hx:62-66 writes best and volume together, on resume/restart/title |
| 5 | Game over: time, level, defeated, new best, Restart and Title | met | late.png: "Time survived: 0:11", "Level 1", "Enemies defeated: 3", "A new best!", Restart and Title |
| 6 | Ground 40 x 40 with grid, low walls nothing passes | met | Main.hx:110-121 40x40 floor, :384-389 grid lines every 2 m; walls 1.2 m high (:124-142, title.png); Game.hx:267-269, 506-511, 366 clamp her, enemies and shots |
| 7 | Camera about 55 degrees from 14 m, following smoothly; a sun and a fill | met | Main.hx:21-22, 372-376 exponential follow at 55 deg / 14 m; :74-88 directional sun with shadows and a fill from above; play shots |
| 8 | Warden from the four Adventurer parts on one skeleton, animated together | met | Game.hx:100-101 four Adventurer parts; Character.hx:44-48, 108-113 each part given Animations.glb and played alike; play/move shots show one coherent body |
| 9 | Moving: WASD, arrows, stick; 5 m/s per time; no faster diagonally; faces movement | met | Main.hx:163-168 bindings; Game.hx:255-270 normalised when len>1, speed*dt at fixed tick, heading = atan2; move.png she faces right while D held |
| 10 | Touch stick where a finger lands on the play area; stops on lift | met | Main.hx:184-202 bindTouchStick over the play area minus the button column (and between the buttons); :556-564 drawn at its origin once touched; axes added to movement :297-298 |
| 11 | Touch buttons: pause top right, switch bottom right with both weapons; neither moves her; menus tapped | met | Main.hx:200-201, 545-555 pause button top right, switch drawn only with the gun, stick regions exclude both; Menu.hx uses the pointer that touch drives |
| 12 | Animations: Idle, Run, attack, HitRecieve, Death | met | Game.hx:139-149 clips by CharacterArmature| names, used at :273-281, 287-294, 476, 396, 534; title (idle), move (run/attack), play-later (her death) all posed correctly |
| 13 | Attack from the waist up while moving, legs running on | met | Game.hx:287-294 playLayer masked at Abdomen while moving, whole body standing; move.png legs in stride with the sword swung |
| 14 | Blends over 0.2 s | met | Game.hx:16 FADE = 0.2 passed to every play/playLayer/stopLayer |
| 15 | Materials as authored, metallic and roughness honoured | met | parts keep their glTF materials (only a tint, white for her); shots show distinct cloth, skin, boots and a metal blade |
| 16 | Weapon in her right hand through every animation | met | Game.hx:129-137 weapon nodes reparented to her Middle1.R (and again on a body swap, :102); title/play/move/crowd shots show the sword gripped in her right hand |
| 17 | Sword: 0.8 s, 2 m, 120 deg arc, 10 damage | met | Game.hx:17-18, 298-315: every 0.8*quickness, reach + body radius, cos 60 arc test, swordDamage 10 |
| 18 | Gun: 0.5 s, nearest in 12 m and 90 deg, glowing sphere 20 m/s, first hit, 6 damage | met | Game.hx:19-23, 316-345 nearest sorted within 12 m and cos 45; Fx.hx:53-55 small unlit over-bright sphere; :359-385 moves 20 m/s, first enemy hit, 6 damage |
| 19 | Both weapons: last picked held; Q or shoulder switches | met | Game.hx:622-625 gun card sets holding = GUN; :242-245 switch; Main.hx:172-174 Q and both bumpers |
| 20 | Health 100, bar above her head facing the camera | met | play.png/move.png green bar over her head; Main.hx:390-401 rectangle3d tilted by the camera pitch; Game.hx:57 hp 100 |
| 21 | Enemies: other outfits' parts, tinted red, running | met | Game.hx:442-450 outfits 1..9 (never Adventurer) tinted (255,70,60) playing Run; play/crowd shots show red, whole, running bodies of varied outfits |
| 22 | Spawn at the edge out of view where they can; 2.5 m/s at her; pushing apart | met | Game.hx:420-439 points 19 m out on a side, up to 8 tries for one out of view; :463-470 2.5 m/s at her; :481-504 pairwise push; crowd.png crowd closing from all sides, not overlapping |
| 23 | Contact 8/s; enemy health 20 +5 a minute; HitRecieve, Death; sinking | met | Game.hx:471-472 8*dt while touching; :448 20 + 5*minutes; :402 HitRecieve, :396 Death; :517-523 sinks after the clip, then removed |
| 24 | Waves: 5, +10 every 30 s, up to 200 | met | Game.hx:241 min(200, 5 + 10*floor(t/30)); :197 start at 5 |
| 25 | Gems: dropped, small, green, spinning; pulled within 3 m; 1 xp | met | Game.hx:540-572 octahedron gem, green emissive (Fx.hx:57-62), spinning, flies within pull = 3, xp++; play-later.png bar at 3/5 after 3 kills |
| 26 | Death bursts: 40 red sparks, flying and falling, additive, 0.6 s fade; 2,000 | met | Fx.hx:9-41, 66-73: 50 emitters x 40, AlphaMode.ADD, life 0.6, red to alpha 0, upward cone with gravity -12 |
| 27 | Target ring: thin, sword's reach, pulsing once a second, own shader on ground coords | met | shaders/ring.glsl: distance from wgf_world_pos.xz to center, 0.08 m wide, sin(time*2pi) pulse; Fx.hx:81-87 radius = swordReach; cyan ring in play shots |
| 28 | Spawn warnings: red circle filling from the middle over 1 s, own shader, fill passed in | met | shaders/warning.glsl fills to fill*radius; Game.hx:409-418 fill = t/1.0 set each tick, enemy made at 1 s, only for spawn points in view |
| 29 | Hit flash: white 0.1 s, still animating, own shader on the lit animated enemy | met | Game.hx:390, 512-515 flash 0.1 s; Character.hx:141-157 swaps each slot to shaders/flash.glsl (lit by the stage's lights, mixed 85% to white) on the skinned parts, animation untouched |
| 30 | XP bar along the top; level 2 at 5, each level +5 | met | play-later.png green bar along the top with "Lv 1"; Game.hx:574-585 xpToNext = 5*level |
| 31 | Level up: play stops, three cards with name, icon, text; click, tap, arrows+Enter, pad | met | Main.hx:301-304 world paused, LEVEL_UP state; Menu.hx:149-164 panel card with icon, name, wrapped text; :63-72 left/right, Enter, d-pad, south; click/tap via pointer |
| 32 | Cards: Sharper blade, Quick hands, Long reach, Gun once, More bullets | met | Card.hx:13-17 names; Game.hx:614-627 +5 damage, x0.85 time, +0.5 reach, gun only offered without it, +1 shot to the next nearest |
| 33 | Gear: part of an outfit she isn't wearing, swapped at once without a jump, its bonus | met | Game.hx:604-612 outfit not in any slot; Character.hx:84-106 new part put on next update with syncAnimation from a staying part; Game.hx:628-641 +10 max hp, +10% armor, +0.5 m/s, +1 pull; named "Witch Head" style, outfit icon |
| 34 | Cards and buttons from panel.png and button.png as nine-slices | met | title.png and pause.png show the Kenney panel and button art with clean corners; Menu.hx:93-107 textureNineSlice with slices.txt's margins (16, 14 per 190x49 cell) |
| 35 | HUD: xp bar and level, time, defeated and FPS, gear and weapon icons; screen-fixed | met | play.png: "Lv 1" and bar, "0:03" top left, "Defeated: 0" and "FPS" top right, four Adventurer gear icons with slot badges and the sword icon at bottom; drawn in screen space |
| 36 | Sound: five effects, music looping and ducked to half when paused, all on the volume | met | Game.hx:303, 323, 475, 565, 583 slash/shot/hit/gem/level; Main.hx:238-245 music.ogg looped on the music bus, :259 faded to 0.5 when paused; :454 slider sets the master bus |
| 37 | --enemies=N and --seed=N; same seed same enemies | met | Main.hx:61-62 App options; Game.hx:197, 241 cap held at N; :202 spawner seeded, play on a fixed 60 Hz tick (only caveat: the in-view test reads the frame-smoothed camera) |

Counts: met 37, wrong 0, missing 0, can't tell 0.
