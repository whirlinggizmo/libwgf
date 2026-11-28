# Wardens, three.002: grades

| # | item | grade | seen |
|---|---|---|---|
| 1 | Window 1280 x 720, resizable; arena framed, HUD at edges on resize | met | main.js:29-38 resize keeps the 16:9 horizontal view and widens the vertical field when the window is narrower; the HUD is a DOM overlay pinned to the edges (ui.js:23-28) |
| 2 | Title: "Wardens", best time, Play and Quit, warden idling and turning | met | title.png: "Wardens", "Best: 0:00", skinned Play and Quit; the warden whole, idling, sword in her right hand; turns at 0.6 rad/s (main.js:608) |
| 3 | Play goes fullscreen; Quit ends the game | met | main.js:230 requestFullscreen on start; quitGame main.js:235-242 stops the loop and audio, tries window.close and shows "Thanks for playing" |
| 4 | Pause on Escape or P: Resume, Restart, Title, volume 0-100% saved | wrong | the panel and the code are right (ui.js:125-136, main.js:139-141, 651), but pause.png, a second after Escape, shows play running with no panel; it only paused on the second Escape (play-later.png at the same 0:06), so the first Escape while fullscreen doesn't pause |
| 5 | Game over: time, level, kills, new best, Restart and Title | met | main.js:514-523 and ui.js:138-144 set all four lines and both buttons; no shot shows her dead |
| 6 | Ground 40 x 40 with grid, low walls nothing passes | met | main.js:46-60 40 m plane, 40-line GridHelper, wall models scaled 0.3 high around the edge; warden clamped at 19.6 m (418) and enemies at 19.7 m (474); wall seen in pause.png |
| 7 | Camera about 55 degrees from 14 m, smooth follow; sun and fill | met | main.js:564-574 14 m at 55 degrees, eased follow; DirectionalLight and HemisphereLight at 40-42; play.png, move.png |
| 8 | Warden from four Adventurer parts on one skeleton | met | chars.js:105-120 binds every part's meshes to Adventurer_Body's skeleton, one mixer; the body is whole and in proportion in title.png, play.png and move.png |
| 9 | Moving: WASD, arrows, stick; 5 m/s, diagonal normalised; faces movement | met | main.js:198-213, 412-424: keys normalised, speed times dt (dt capped at 0.05, so below 20 fps she slows); turns towards the move direction; move.png shows her moved and facing right |
| 10 | Touch stick where the finger lands, like the left stick, stops on lift | met | main.js:161-181 stick placed at the touch on the play area (buttons skipped), cleared on pointerup/cancel; read as the stick at 211 |
| 11 | Touch pause top right, switch bottom right with both weapons; menus tapped | met | ui.js:99-100, 47-48 buttons at the top right and bottom right, shown after a touch (main.js:153-160) and the switch only with the gun; main.js:164 a press on a button doesn't start the stick; menu buttons and cards take clicks |
| 12 | Animations: Idle, Run, attack, HitRecieve, Death | met | title.png idle, move.png running with a slash, enemies running in crowd.png; HitRecieve and Death at main.js:350, 345, 481-482 |
| 13 | Attack from the waist up while moving, legs running on | met | chars.js:27-32 splits tracks at Hips/legs; main.js:354, 530-533 put the attack on the upper layer only while moving; move.png shows a slash over a running stride |
| 14 | 0.2 s blends | met | chars.js:52-63 each layer's weights move linearly towards the target over 0.2 s |
| 15 | Materials as authored | met | chars.js:113 the warden's meshes keep their glTF materials; cloth, skin and boots shade differently in title.png |
| 16 | Weapon in her right hand, following it | met | chars.js:121-129 sword and gun parented to the Middle1.R bone with their own local transforms; held in her right hand in title.png, play.png and mid-slash in move.png |
| 17 | Sword: 0.8 s, 2 m, 120 degrees, 10 damage | met | main.js:356-365 reach 2 (G.reach), half-angle pi/3, dmgSword 10; main.js:490 cooldown 0.8 times the multiplier |
| 18 | Gun: 0.5 s, nearest in 12 m and 90 degrees, glowing sphere at 20 m/s, first hit, 6 damage | met | main.js:367-404 filters 12 m and pi/4, sorts by distance, bullet at 20 m/s hits the first enemy within 0.55 m for 6; yellow sphere with an additive halo (634-640); cooldown 0.5 at 493 |
| 19 | Both weapons: last picked held, Q or shoulder switches | met | main.js:262 picking the gun holds it; Q at 142, pad buttons 4/5 at 193, 596 |
| 20 | Health 100, bar above her head facing the camera | met | play.png and move.png show the bar over her head; main.js:556-557 copies the camera's rotation; hp 100 at 114 |
| 21 | Enemies: other outfits' parts, tinted red, running | met | chars.js:140-185 merged parts of the nine other outfits with a red tint; crowd.png and pause.png show mixed outfits (witch hats, hair) in red and running poses, none distorted |
| 22 | Spawn at the edge out of view, 2.5 m/s at her, pushing apart | met | main.js:312-327 edge points, up to 12 tries for one out of view; 452 moves at 2.5 m/s towards her; 457-467 push apart at 0.8 m; crowd.png shows them closing in without piling up |
| 23 | Contact 8/s; health 20 +5 a minute; HitRecieve, Death; sinking | met | main.js:477-478 8 a second for each enemy touching; 333 health 20+5 a minute; 345, 350 Death and HitRecieve; 446-447 sinks then freed |
| 24 | Waves: 5, +10 every 30 s, up to 200 | met | main.js:311 min(200, 5 + 10 per 30 s) |
| 25 | Gems: dropped, small green spinning, pulled within 3 m, 1 experience | met | main.js:348 dropped on a kill; 627 small green octahedra; 545-546 spin; 503 pull at W.pull 3; 508 addXp(1); none visible in the shots |
| 26 | Death bursts: 40 red sparks, flying and falling, additive, 0.6 s fade, 2,000 | met | fx.js:4-51 2,000-point pool, 40 a burst (main.js:347), velocity plus gravity, additive blending, alpha fading over 0.6 s, red |
| 27 | Target ring: thin, sword's reach, pulsing once a second, own shader from ground coordinates | met | fx.js:54-80 shader on world xz around the warden, radius G.reach, sin(2 pi t) pulse; play.png shows a thin ring about 2 m in radius |
| 28 | Spawn warnings: red circle filling over 1 s, own shader, fill passed in | met | fx.js:83-126 shader with uFill set to t/1.0 by the game, red fill growing from the middle; used for spawns in view (main.js:325-326); none in the shots |
| 29 | Hit flash: white 0.1 s, still animating, own shader on lit enemy | met | chars.js:166-173 onBeforeCompile on the lit standard material mixes towards white by uFlash; main.js:342, 442 for 0.1 s; the enemy's mixer keeps running |
| 30 | Experience bar along the top; 5 then +5 a level | met | ui.js:23-25 bar across the top, seen in play.png and crowd.png; main.js:131 needFor = 5 times level |
| 31 | Level up: play stops, three cards with name, icon, text; click, tap, arrows and Enter, gamepad | met | crowd.png shows three cards with names, icons and text over a stopped game; main.js:143-147, 598-604, ui.js:156 |
| 32 | The cards as specified | met | main.js:257-263 Sharper blade +5, Quick hands x0.85, Long reach +0.5, Gun only without it, More bullets adds a shot at the next nearest (gunFire 379) |
| 33 | Gear: random part of an unworn outfit, swapped at once, bonus | met | main.js:264-284 random slot and an outfit not worn, named "Medieval Head" (crowd.png), swapped on the shared skeleton, bonuses +10 hp, x0.9 damage, +0.5 m/s, +1 m pull |
| 34 | Cards and buttons nine-sliced from panel.png and button.png | met | ui.js:37, 42, 53 CSS border-image slices; title.png and pause.png show the panel and buttons with unstretched corners |
| 35 | HUD: bar and level, time, kills and FPS, gear and weapon icons; none turns | met | play.png: "Lv 1" on the bar, 0:03 top left, "Defeated: 0" and "60 FPS" top right, four gear icons and the sword at the bottom; a DOM overlay |
| 36 | Sound: five effects, music looping and ducked when paused, all by volume | met | main.js:357, 378, 480, 508, 290 the five effects; 95-100 music looped and halved when paused (217); master gain from the slider (87) |
| 37 | Run options enemies and seed; same seed, same enemies | met | main.js:7-9 from the page's query, its command line here; crowd.png shows about 200; the seeded mulberry drives spawn points and cards (12, 111), and outfits come from pool order |

Counts: met 36, wrong 1, missing 0, can't tell 0.
