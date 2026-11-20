# Wardens on libwgf: notes

Helix's engine trial (`../helix/docs/trials/templates/wardens/`: `spec.md` and its assets, unchanged), written in Haxe on
libwgf's public API through its binding, as the trial's entrants were asked to write it: from the API's headers and docs,
no private header, the program run from its directory naming `assets/...`, taking `--enemies=N --seed=N`, and ending
itself on `--frames=N`. These are the notes the briefs ask for. libwgf itself was not changed for it.

## Build errors, in order

1. `ActorDestroy.CHILDREN`: no such field. The binding names `WGF_ACTOR_DESTROY_CHILDREN` `ActorDestroy.DESTROY_CHILDREN`:
   it strips the prefix its values share (`WGF_ACTOR_`, as the other is `WGF_ACTOR_KEEP_CHILDREN`), not the type's name.
   Changed every use. The only compile error.

## Mistakes found by running it

1. `Window.setSize(1280, 720)` fixed the web canvas at that size at the page's top left, cutting the HUD off in a smaller
   browser window. `wgf_window.h` says so; removed. The presentation (`EXPAND`, 1280 by 720) opens the native window at its
   design size and fills the page on the web.
2. The first playthrough autopilot stood still, and she died at 14 seconds: the spec's 8 damage a second from each enemy
   touching her kills a warden who doesn't move. The playthrough steps out and turns back, so the sword meets them.

## Where libwgf had no call for what the spec asks, and what I did

- **Tinting a character, and the hit flash.** A model's tint and materials are its own: on a glTF file's root they reach none
  of the file's node models. `Character` walks each part's tree once it has been made (polled: the root's child count turns
  above 0, there being no event for it) and tints each model with a mesh. gfx-skinning's `set_below` does the same walk.
- **The hit flash's colors.** A custom material can't read the built-in material it replaces, so the flash shader is given
  each slot's base color: one flash material per part mesh and slot (`Fx.flashMaterial`), set on every slot of every node
  model for 0.1 s, the tint white meanwhile, then the slots given back (`setMaterial(-1, 0)`).
- **The weapon in the hand.** Sword.glb and Gun.glb hold the whole skeleton, the weapon a node under `Middle1.R`. There is no
  call to attach another file's node, so the weapon's node is found in its own file (`findNode("Sword")`, `"Pistol"`) and
  parented to the body's `Middle1.R` (`findNode`), keeping its local transform: the bind pose's place in the hand. The weapon
  file's root stays alive, undrawn, holding the file. When the body is swapped the nodes move to the new body's bone first.
- **A loop per clip.** Looping is the model's (`setAnimationLoop`), not a play's: it is set before each play (Run and Idle
  loop; an attack, Death, played once).
- **The upper-body attack while moving** is layer 0 on `Abdomen` (Hips and the legs run on); `HitRecieve` on layer 1 for the
  warden, so a hit doesn't cut an attack off, and layer 0 for the enemies, who keep running.
- **The menus.** libwgf's immediate UI (`Ui`, Clay) draws flat colored widgets; its documents (RmlUi) were the other way. I
  drew the menus myself (`Menu.hx`, about 200 lines): `Draw.textureNineSlice` for the panel and each button state, my own
  focus (arrows, d-pad, the pointer), choosing (click, tap, Enter, Space, pad south), the slider and the cards. A skin for
  `Ui`'s widgets (an image region per state, nine-sliced) would have saved all of it.
- **The health bar facing the camera.** No billboard rectangle: `Draw.rectangle3d` turned back by the camera's fixed 55
  degree pitch, which faces it since the camera never turns.
- **Touch: the stick off the buttons.** A stick's region is one rectangle, and the buttons are in two corners: two sticks,
  one left of the buttons' column and one between them, their axes summed. Bound to the visible area, and bound again
  (`Action.clear`) when it changes.
- **Death bursts.** I wasn't sure an emitter moved and burst twice in one tick bursts at both places, so there are 50
  emitters of at most 40 sparks, taken in turn (2,000 at most).
- **Out of view** is `Camera3d.worldToScreen` of the spawn point against the visible area.
- **Many actors.** Each part's tree is 64 actors, so an enemy is 256 and 200 of them 51,200, under the 65,535 a handle kind
  can have. Enemies alive, coming and dying are held to 240 together.

## Guesses

- The parts face +z (they do). Low walls: the kit's wall scaled to 1.2 m high and half as thick, just outside the 40 m square.
- The waist: the `Abdomen` bone. Touching: centers 0.75 m apart; the enemies stop at 0.6.
- An enemy's parts each from a random outfit other than the Adventurer, all four tinted red.
- `HitRecieve` from the waist up (above). A spawn warning's circle is 0.7 m across.
- Cards: three different ones a level; Gun is offered until she has it, More bullets only once she has. A gear card's outfit
  is one she wears in no slot.
- The music stops at the title, and is ducked at the game over as when paused. Quit is shown disabled on the web, where
  quitting means nothing (`App.canQuit`).
- The volume is the master bus's, saved with the best time in `user:wardens.save` when the pause menu is left.

## What I expect not to work, or couldn't try

- Touch: never tried (no touch screen here). Nor a gamepad by hand.
- Sound: every run here had no audio device.
- Speed at 200 enemies on a GPU: not measured here. Its CPU side, measured once (a release headless build, `Loop.getFrameCost`
  over 600 frames with 180 or more alive): a median of 8.2 ms a frame, a 95th percentile of 14.6, the worst 21. Most of
  it is posing 800 file roots of 64 nodes each; the game's own tick (pushing apart is all pairs, 20,000 at 200) is a
  small part. A stutter at 200 is possible on a slow CPU. On Mesa's software renderer the crowd draws at about 10 frames a
  second, and the game's ticks fall behind real time (the loop's catch-up is capped), so the screenshots show less game
  time than wall time.
- A real pad plugged into the machine is read by the program on a private display too (evdev isn't the display's): a
  drifting stick would move her in a grader's run, past the 0.15 dead zone I bind.
