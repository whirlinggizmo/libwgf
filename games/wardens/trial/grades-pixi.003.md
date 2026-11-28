# Wardens, pixi.003: grades

| # | item | grade | seen |
|---|---|---|---|
| 1 | Window 1280 x 720, resizable; arena framed, HUD at edges on resize | met | main.js:5 canvas `resizeTo: window`; game.js:356 relays out screens and HUD on resize; game.js:731 widens the fov for aspects narrower than 16:9 |
| 2 | Title: "Wardens", best time, Play and Quit, warden idling and turning slowly | met | title.png: "Wardens", "Best: 0:00", Play and Quit, warden whole with the sword in her right hand; game.js:700 turns her at 0.6 rad/s on Idle |
| 3 | Play goes fullscreen; Quit ends the game | met | game.js:428 requestFullscreen on Play; game.js:431-436 Quit stops the ticker, blanks the page and calls window.close |
| 4 | Pause on Escape or P: Resume, Restart, Title, volume slider 0-100%, saved with the best | wrong | pause.png, a second after Escape, shows no panel and no dim; the panel only appears in play-later.png after the second Escape. Code: game.js:201 keys, 307-315 panel, audio.js:3,10 volume in the same `wardens` store as best |
| 5 | Game over: time, level, enemies defeated, new best, Restart and Title | met | game.js:318-327 and 455-463 set "Time survived", "Level", "Enemies defeated", "New best!" with Restart and Title; she never died in a shot |
| 6 | Ground 40 x 40 with a grid, low walls nothing passes | met | walls at ±20.25 (game.js:52-61), scaled 0.3 high; grid by shader (fx.js:49-71); warden clamped to ±19 (595), enemies ±19.3 (621), shots die at 19.5 (648); grid in every play shot |
| 7 | Camera about 55 degrees from 14 m, following smoothly; a sun and a fill | met | game.js:720-729 pitch 55, distance 14, exponential follow; game.js:38-40 a DirectionalLight and an ambient fill, plus a sky/ground environment; play.png framed on her |
| 8 | Warden from the four Adventurer parts on one skeleton, animated together | met | game.js:95 wears the four Adventurer parts; anim.js:183-200 writes one shared pose into each part's skeleton; move.png and play.png show one coherent body |
| 9 | Moving: WASD, arrows, stick; 5 m/s by dt, not faster diagonally; faces her movement | met | game.js:585-597 keys and pad, normalised, speed * dt, turns toward motion; move.png shows her moved right with the camera following (dt capped at 0.05, game.js:697) |
| 10 | Touch stick where a finger lands on the play area, moves as the left stick, stops on lift | met | game.js:213-221 starts the stick only on a touch whose target is the stage, ends it on up/cancel; game.js:590 feeds it into movement |
| 11 | Touch buttons: pause top right, switch bottom right with both weapons; neither moves her; menus tapped | met | game.js:279-291 pause at top right, switch at bottom right shown only with the gun; 284 stops propagation; ui.js Button/Card/Slider take pointer taps |
| 12 | Animations: Idle, Run, attack, HitRecieve, Death | met | anim.js:44 loads all six clips and the Animator layers them; move.png shows Run with a Sword_Slash, enemies Run in every shot |
| 13 | Attack from the waist up while moving, legs running on | met | anim.js:41-43 marks Abdomen and below it as upper; anim.js:143 masks the attack to them while running; move.png crop shows the slash over a running stride |
| 14 | Changes blend over 0.2 s | met | anim.js:126 Idle/Run blend at dt/0.2; attack, hit and death fade in over 0.2 (139, 151, 156) |
| 15 | Materials as authored, metallic and roughness honoured | met | parts drawn with the glTF's own Material3D (game.js:83); enemies clone them (fx.js:80-87); shots show the outfits' varied surfaces |
| 16 | Weapon in her right hand, following it | met | Sword.glb bound to the same pose, its Middle1.R node posed (anim.js:177-200, game.js:709); title.png and move.png show the sword in her hand through idle and slash |
| 17 | Sword: 0.8 s, 2 m, 120 degree arc, 10 damage | met | game.js:552-562: reach 2, dot >= 0.5 (120 degrees), damage 10, cooldown 0.8 * cd (it swings only when an enemy is in reach) |
| 18 | Gun: 0.5 s, nearest within 12 m in 90 degrees, glowing sphere 20 m/s, first hit, 6 damage | met | game.js:563-577 range 144 squared, dot >= 0.7071, nearest first; 540-546 bright unlit sphere at 20 m/s; 649 first enemy hit for 6 |
| 19 | Both weapons: last picked held, Q or shoulder switches | met | game.js:399 the Gun card holds the gun; 202 Q and 680 buttons 4/5 call switchWeapon (445-450) |
| 20 | Health 100, a bar above her head facing the camera | met | play.png green bar over her head; game.js:99-103 billboarded quads, 171 hp 100 |
| 21 | Enemies: other outfits' parts, tinted red, running | met | game.js:22,147 non-Adventurer parts; fx.js:72-87 red tint; play.png, move.png, crowd.png show red figures running, bodies whole |
| 22 | Spawning at the edge, out of view where possible; 2.5 m/s at her; pushing apart | met | game.js:478-488 edge at 18.6 with up to six tries for out of view; 607-612 2.5 m/s straight at her; 500-523 separation; crowd.png shows them spread round her |
| 23 | Contact 8/s; health 20, +5 a minute; HitRecieve and Death; sinking away | met | game.js:640 8 * armor * dt; 160 20 + 5 per minute; 529-533 Death or HitRecieve; 627 sinks 1.6 m over 0.8 s then released |
| 24 | Waves: 5 at start, +10 every 30 s, to 200 | met | game.js:473 cap = min(200, 5 + 10 * floor(t/30)) |
| 25 | Gems: dropped, small, green, spinning; pulled within 3 m; 1 experience | met | game.js:535-539 green emissive crystal on each kill; 655-662 spins, pulled within G.pull 3, +1 xp; none visible in a shot |
| 26 | Death bursts: 40 red sparks, flying and falling, added, 0.6 s fade; up to 2,000 | met | game.js:530 burst of 40; fx.js:105-143 capacity 2000, blend add, gravity, alpha fading over 0.6 s, red |
| 27 | Target ring: thin, sword's reach, pulsing once a second, own shader on ground coordinates | met | fx.js:5-24 ring from vPosition.xz, pulse sin(uTime * 2 pi); game.js:736 radius = reach; play.png thin cyan ring around her |
| 28 | Spawn warnings: red circle filling over 1 s, own shader, fill passed in | met | fx.js:27-46 warn shader with a fill uniform; game.js:484-494 passes fill = t / 1 s then spawns; none caught in a shot |
| 29 | Hit flash: white 0.1 s, still animating, own shader on the lit enemy | met | fx.js:73-78 mixes to white after lighting on the cloned lit material; game.js:533,613 flash 0.1 s while the pose keeps updating |
| 30 | XP bar along the top; level 2 at 5, each +5 | met | play.png bar along the top; game.js:264,662 need 5 * level |
| 31 | Level up: play stops, three cards with name, icon, text; click, tap, arrows and Enter, gamepad | met | crowd.png three cards with name, icon and text over a stopped game; game.js:364-392 cards tap, FocusGroup takes arrows/Enter and the pad |
| 32 | The cards as the spec gives them | met | game.js:395-400 Sharper blade +5, Quick hands x0.85, Long reach +0.5, Gun once, More bullets |
| 33 | Gear: random part of an outfit not worn, swapped at once, its bonus | met | game.js:401-408 random slot and outfit other than the one worn there, named "Soldier Legs" (crowd.png), wearPart applies the current pose (87); bonuses as given |
| 34 | Cards and buttons nine-sliced from panel.png and button.png | met | ui.js:259-262,275 NineSliceSprite; title.png, play-later.png and crowd.png show the panel and buttons with unstretched corners |
| 35 | HUD: XP bar and level, time, defeated and FPS, gear and weapon icons; none turns | met | play.png: bar and "Lv 1" top left, "0:03", "Defeated" and "FPS" top right, four gear and the sword icon at the bottom, all screen-space |
| 36 | Sound: five effects, music loop ducked to half when paused, all following volume | met | game.js:383,562,576,642,665 plays level, slash, shot, hit, gem; audio.js:36,39 music loops and ducks to 0.5 on pause; everything goes through the master gain |
| 37 | Run options enemies and seed; same seed same enemies | met | game.js:9-19 enemies and seed from the page query (the run's command line); crowd.png shows the 200; spawns and outfits from seeded mulberry32 |

Counts: met 36, wrong 1, missing 0, can't tell 0.
