# Command and Conquer: Generals: Zero Hour: Reforged

EA opened the source of Generals and Zero Hour, the game that is still on sale and still runs on
Steam. This build is that source with the bugs the game shipped with fixed and everything below added.

**125 changes. ~580 engine source files ported. 14 automated test suites. Around 60 original bugs
found and fixed â€” EA's own, not port damage.**

---

## The frame rate cap is gone

- The picture now runs uncapped; the rules keep their own steady clock.
- On a machine with two graphics chips, the game used to draw on the first one it found, which was the slow one. It now uses the dedicated card, and a match holds about 260 frames a second with the trees, the shadows and the filtering left on.
- Display options has a vertical sync box. Off, the picture still runs free. On, it waits for the monitor so the frame does not tear. The box takes effect when you Accept.
- A slow moment costs you a dropped frame, not a slow game.
- Explosions no longer catch the picture. The first time each kind of fire, smoke or blast reached the screen, the game stopped for 25 to 60ms to prepare it, and the opening volley of a fight cost 300ms at once. That work is kept on disk and ships with the game, so it happens while the map loads, in about 3ms, and the worst frame of the same battle went from 141ms to 38ms.
- The price, build time and power figures on the command bar buttons stopped costing you frames. All the buttons shared one piece of text, so every figure on the bar was lettered from scratch every frame, about 33 of them. Each now keeps its own: a battle that averaged 14ms a frame averages 11.7ms, frames slower than 60 a second went from one in six to one in a hundred, and the old renderer gets about 0.8ms back too. The money readout was lettered again every frame as well, for a number that had not changed; it is lettered when the number moves. Over a whole battle the game now letters 163 pieces of text where it lettered 68,227.
- A screen full of fire and smoke is cheaper to draw. Thirty Inferno Cannons shelling twenty Crusaders used to send the graphics card 459 separate batches of see-through effects a frame, mostly the same texture again and again, and the game looked up the same seven card settings from scratch for every one of them. Matching effects go out together now, and settings that have not changed are not looked up again: 177 batches, and the same battle went from 11.6ms a frame to 8.6ms. It is at 6.7ms now, since the game stopped preparing everything it draws a second time for a copy of the picture nobody sees. Nothing on screen changes, the effects are drawn in exactly the order they were.
- The same battle with the particle limit turned up costs 7.6ms a frame where it cost 9.1ms, and the old renderer went from 9.3ms to 6.3ms. Both were keeping a count of every kind of draw the game makes, and both did it by writing out and reading back a description of each one, on every draw, for a report no player ever opens. They glance at one draw in sixteen now. The new renderer also stopped re-sending the graphics card settings it had just sent.
- Raise the particle limit as far as you like and every puff gets drawn. Past about sixteen thousand smoke and fire sprites on screen the game quietly stopped drawing the rest: a test with 68,000 of them showed barely a fifth. A test with 101,000 now draws all of them at 8.3ms a frame, and in 1,200 frames not one fell below 60 a second. The first version that drew them all took 22.4ms; the rest came from sorting and copying them far less and from sharing the work across every core of the processor.
- Every animation runs on a clock instead of counting frames.
- A scroll that starts after the camera has stood still no longer lurches. The first frame of the move was timed from the last time the camera moved, so after a few seconds of looking at one spot it was worth three frames of scrolling at once. The clock runs while the camera is still now, and the first frame moves one frame's worth.
- Briefing and cutscene subtitles hold long enough to read again.
- The radar's under-attack pulse throbs instead of strobing, and no longer ends early.
- The main menu surf rolls at its proper pace, not ten times too fast.

## The computer opponent builds a base now

- One wrong value meant the AI built only urgent items and one power plant.
- Rotated AI bases were laid out wrong; buildings now face the right way.
- An AI with no buildings no longer aims everything at the map corner.
- The AI checks ten seconds of supply-line damage, not a third of one.
- One AI decision read leftover memory, so identical matches played out differently.
- Difficulty and AI money change build pace again; every order was pinned at three seconds.
- An urgent building it could not put up yet no longer stops the whole base. The computer always builds its most urgent request first, and when that request needed something it did not have, it built nothing else either: a Hard China that asked for a bunker before its barracks stood sat on 35,000 until well into the match. It now skips to the first urgent request it can actually build.
- Hard USA opens the way a player does. It used to start on a barracks and hold its first power plant until a second dozer existed, so its first harvest came in at the 74 second mark. It now puts down the power plant and the supply centre straight away and the barracks after them. Over 20 Hard USA against Hard China matches on Tournament Desert, seats swapped both ways, USA won 15 where it won 10. The same opening was tried on China and lost it 17 matches instead of 10, and on GLA it decided too few matches to say anything, so both keep the opening they shipped with.
- The computer no longer shoots at what it cannot see. Its units, and its base defences, used to auto-target through the fog of war - a hard exemption written into the code for computer players only. Stealth works against it now, and so does anything it has not scouted.
- It scouts. There was no such thing as scouting in the computer opponent - it never needed to look, because it could already see everything. One cheap unit now tours the enemy start positions and keeps going round for the rest of the match, replaced out of spare change when it dies. Every difficulty scouts; an opponent that never looks reads as broken, not as easy. And it tours on purpose rather than in a circle: it goes to whichever base it has gone longest without seeing, nearest first, and it does not walk across the map to look again at something it looked at a minute ago - it stays and watches instead. Two scouts never trail each other round the same lap.
- It takes the oil derricks instead of blowing them up. An oil derrick pays whoever owns it and costs one infantryman to walk in - and the computer used to march past the neutral ones and shell yours, which is the one thing you can do with a derrick that earns nobody anything. It now keeps a cheap infantryman doing the rounds of every derrick, refinery and hospital it has found, takes them, and when there is nothing left to take it leaves him standing on the last one instead of walking him home. And it no longer picks a capturable building as a target of its own accord: it can still be ordered to level one, and one that shoots back is still shot back at.
- Nor does it know which start position you took. It used to read that straight off the lobby, for every player, from the first second. Now it works it out: each position it has not looked at is a suspect, and the odds are simply how many opponents are still unaccounted for over how many places they could be - three of you on an eight-position map is 3 in 7 for each. Every empty position its scout crosses off makes the rest likelier, 3 in 6, then 3 in 5. When the numbers meet, it stops looking: three opponents with three places left to be is not a guess, and walking over to confirm it would waste the trip. On a two-player map that is true from the start, so nothing changed there - it never wastes a second hunting for an address it can work out by subtraction. Where it has not worked you out yet, it attacks the position you are most likely to be on, and finds out on arrival.
- Nor does it read your base off the map. Where your base is, what it is worth, which supply dock to expand to and where to aim a superweapon all came off a walk of your object list, in the shroud, from the first second of the match. The computer now only counts what it can see, plus the buildings it has already found - buildings do not walk away. Before it has scouted you, all it knows is where you started, which is on the map preview anyway.

## Generals powers the computer actually meant to buy

- It spent every promotion point the moment it had one, on whatever in its list happened to be cheap enough. The strong three-point ability at the end of the list was never reached, because the points were always already gone.
- From Medium up it saves. If the next thing in its own list is only out of reach on points, it waits for them instead of buying filler.
- Which list it draws from now follows its personality, so the powers coming at you tell you which kind of opponent you are facing.

## The computer comes in where you are thin

- The game has kept a value-and-danger map of every square of the battlefield, per player, since 2003. It has a query interface and even a debug view. Nothing in the computer opponent had ever read it - its only users were two map-script actions almost nobody used.
- At the top level the opponent now aims its attacks at where your money is rather than at the middle of your buildings, which is also the middle of your defences.
- It reads that map through the fog like everything else: only ground it has actually seen counts. An opponent that has not scouted you still attacks the old way.

## The computer expands, and defends what it takes

- It never decided to expand. The machinery was all there - place a supply centre beside a pile, pick a pile worth taking, send a team to sit on it - and every piece of it only ran when a map script said so. An opponent that ran its starting piles dry simply stopped earning.
- It now goes and takes the money that is lying around, out of spare cash so the army never pays for it.
- On Hard the expansion comes with a defence structure facing the enemy, placed in the same job. An undefended expansion is a gift.
- Measured: a third more army in the field and a quarter more money spent over the match.

## The computer spends its money

- It no longer sits on a pile of cash. Past a level that depends on the difficulty, the more money it has the faster it builds - twice the pile, half the wait, and it stops at four times. It is spending sooner, not building faster: the rate itself is untouched.
- A full bank buys a bigger army. Late in a match the computer's plan allows one of each attack group, so once they were all on the map it only replaced its losses and banked the rest: a Hard USA sat on 117,000 on Twilight Flame. Now every 8,000 in the bank on Hard allows one more copy of each attack group, up to four of them.
- Hard builds past its plan. When every war factory, barracks or airfield it owns is busy it puts down another, beside its newest expansion so the army comes out nearer the fighting. When production keeps up it buys income instead, supply drop zones and black markets, as many as the bank pays for. China trains hackers from any idle barracks with no cap, and a hacker left standing still is put to work.
- Hard picks its road into your base. Of the three approaches it takes the one with the fewest guns it has seen along it, and keeps to the one its plan named until it has scouted something. Aiming at your money instead was tried first and walked it straight past your army.
- Hard sends its attack groups out together. A group too small to be a wave on its own waits on the near side of its base until about seven tanks' worth has gathered, or a minute and a half has passed, and then everything waiting leaves on one road at the pace of its slowest unit. A lone artillery piece or a single bomb truck used to drive across the map by itself, and nearly a third of Hard's units died with barely anyone of their own nearby.
- It puts more harvesters on a supply centre with more piles around it, instead of the same three numbers everywhere.
- It keeps its lights on, and keeps them out of reach. Its plan carries a fixed number of power plants and nothing ever adds one, so a raid that took two of them left the base underpowered - radar dark, base defences off, superweapon clock stopped - until a rebuild timer came round and put the new plant back on the spot the raid already knew the way to. It now buys a plant when the margin gets thin rather than when the meter reads zero, and puts it on the far side of the base from whoever it is fighting.
- Spare cash also buys a gun on the side the trouble comes from. Production and income go up around the middle of the base, the power goes behind it, and base defences go out in front, so money that keeps arriving thickens the base in the order a base wants to be thick in. Front means towards the nearest enemy, which is not the same as the one it has decided to attack - that one can be across the map, and a wall built facing it has its back to the neighbour. One gun every minute and a half at most, and it stops at ten of them; a base made of bunkers is a base with no army.

## The computer picks its fights
- Which enemy it goes after was the nearest one and nothing else, plus a rule with its sign the wrong way round: an opponent who had lost his units or his production had his distance treated as half the map, so the computer ignored the one it was about to beat. That is what dragged matches out.
- It now weighs distance, whether the target is crippled - an opportunity, not a distraction - and how much of what it can see that player is worth. Only a genuinely finished enemy is skipped.
- It still refuses to gang up with another computer opponent on one victim, and still gently prefers whoever is already shooting at it.
- An aggressive computer team is supposed to fight its way to where it is sent rather than walk there. It never did. The order to fight was being reissued every frame, and each reissue pushed the moment that unit next looks for a target one step further into the future, so the look never came at all and the team crossed the map through the enemy without firing. Over four and a half minutes of one test match that order was restarted 2,240 times. It is restarted 15 times now.
- And when that switch does happen, the old order stops there. It used to finish its own turn afterwards, steering one last time toward the place the unit was going before the order changed, which is a visible flinch at the head of a column.
## The computer knows when to quit

- Its teams fought to the last man. The word "retreat" appeared nowhere in the opponent's code - the single most visible thing that made it look stupid.
- It now measures the fight, not the health bar: how long its force lasts against how long it needs to finish yours. A unit at a fifth of its health that still out-damages what is shooting it stays; a full-health one being melted leaves. A health percentage gets both of those backwards.
- Two levels of it. On Medium a unit that is personally finished pulls out of a fight its team is still winning. On Hard the whole team breaks off when the exchange is lost. Easy never quits - that is part of what makes it easy.
- It only counts what it can see. An opponent that flinches from something it has not found is reading your unit list again.
- It weighs a fight by what is in it, not by who is nearby: only things that can shoot count, and buildings do not. Its own base is not a reason to feel safe and yours is not a reason to run.
- Its aircraft are left out of it. A retreat order was what took a parked jet off the runway, so while a fight raged near the airfield the whole wing took off, flew at the base, landed, and did it again every few seconds instead of ever reaching you. Aircraft already fly home on their own when the load is spent.
- Matches finish. Two of these opponents used to fail to settle 65% of their games inside sixteen minutes; it is half that now, and they end in under seven minutes on average instead of ten.

## The computer builds against what you field

- Which unit it trained next was a coin flip. It gathered the teams sharing the highest priority number in its data and picked one at random - not one line looked at what it was fighting. An opponent facing nothing but aircraft went on building tanks.
- Now the priority is the start of a score, not the whole of it. What it can *see* you fielding weights the choice, and the weighing is the game's own arithmetic: for every unit it could build and every unit it sees you with, how many shots each needs to kill the other through its armour, how long it waits between them, and what each one costs. A Crusader shell does a tenth of its damage to a rifleman, so your Rebels get Gattlings and your Overlords get Tank Hunters, not whatever the script listed first. Stealth still pushes detectors up. It reads the same unit data the game fights with, so a mod's units are weighed correctly too.
- How much that weighs depends on the level, from nothing at all on Easy - which is what the game always did - to fully on Hard.
- Anti-air was the computer's oldest hole. It is closed.

## Three difficulty levels, and none of them cheat

- Easy, Medium, Hard, the three the game has always had. There were six for a while, with a rung either side of Medium and one above the top; they are gone again, and nothing above the top went with them - Hard now plays what the top of that six-rung ladder played. For a while the top rung was called Brutal on the seat list; it is Hard again, the name the box always printed.
- Every level plays by your rules. No level gets extra money, cheaper units, faster building, longer vision or tougher units - in either direction. What changes is what the computer is allowed to decide: how often it looks at the map, how long it takes to react, whether it counters what you field, whether it masses before attacking, whether it pulls damaged units out.
- Easy looks around and answers far too late. Medium reacts in time, saves its damaged units and goes out to take a second supply pile. Hard is the opponent with nothing held back: no reaction delay at all, it builds against what it can see you fielding, it gathers an army before it commits it, and it takes a losing team off the field.
- Measured, not asserted: Hard beats Easy 15-0 over 32 headless matches with the seats swapped both ways, on twice the army and twice the spending.
- The ladder reads as one ladder wherever a seat is listed. Half of it was named in EA's old words - "Easy Army", "Medium Army" - and half in ours, in the same drop-down, because four separate lists answered "what is this seat called" and no two of them agreed. Every one of them says Easy AI, Medium AI, Hard AI now: the lobby, the seat itself, the game info panel and the online browser.
- All three are in the network and online lobbies as well as skirmish, and the level the host picks is the level every machine at the table plays. A seat also keeps its name, its colour and its start position when the list refreshes, which it did not.
- Making Easy easier means giving it worse decisions, never less money. That is the whole promise.
- The computer also picks a personality each match and keeps it: one plays for the attack, the other for the base. Same resources, spent differently.

## Attack-move actually attacks

- Ctrl on the click advances the group at the slowest unit's pace.
- A plain click lets each unit run at its own speed.
- A unit on attack move looks half as far again as it can shoot, and nothing to do with how far it can see. The two have nothing to do with each other: artillery outranges its own eyes and found nothing this way, while a scout with a long view stopped for things it would spend a minute driving to. One rule, measured against the gun.
- An aircraft only makes that long look forward. It cannot stop and it turns in a wide circle, so a target off to one side or behind is one it has to come all the way round for - which is how a flight of bombers ends up orbiting a corner of the map instead of arriving. Inside sixty degrees of the nose it looks the full distance; outside it, no further than it can already shoot, so it still takes what it is passing.
- Attack-moving through a base shoots the base; buildings were filtered out.
- A new unit heading for its rally point attack-moves there, so it fights what it meets on the way. A dozer, a GLA worker, a supply truck or an empty ambulance has nothing to fight with, and it now just drives there instead of wearing the attack-move order and its colour the whole trip.
- Aircraft make their pass, fly home, rearm and resume your order.
- Nothing walks off the map chasing a target that keeps retreating.
- Being pulled off a chase no longer blinds a unit on the way back. It owes the order a stretch of ground before it is allowed to drive to another fight, and it used to spend that stretch refusing to see anything at all - so a tank rolled past an enemy parked beside it without firing a shot. It still will not go chasing during that stretch. It will shoot whatever is already in front of its gun, which costs the advance nothing.
- A unit turns onto whatever is actually shooting it, then resumes the advance.
- Otherwise it picks the worst thing in range: anything armed before anything that is not, then the more dangerous of the two, and worth fades with distance so it is never the far one it walks the field for. A group ordered through a base no longer stops for the first dozer it passes while the artillery beside it keeps firing.
- Ctrl on the attack-move click no longer shells the ground instead.
- A big selection arrives as a crowd. Two hundred units told to attack-move used to spread along a line and three hundred told to move mostly stood still: the group tried to hold its shape all the way in. Past forty units everyone now heads for the spot you clicked and takes the nearest free ground to it. That ground is handed out from the spot outward, ring by ring, and the edge of a cliff ends it: click on the lip of a ridge and the group fills the top, where it used to send half its tanks down to the foot and back round by the ramp to stand there. Whoever gets there first takes the middle and the slow ones take the edge, and the pile grows back toward the way the group came, so the last tank onto a bridge stops behind the others instead of driving through all of them to the far end. Below forty the group keeps its spacing on the way in, because that is what stops a dozen tanks arriving in single file. A waypoint queued with alt is the exception and now keeps every unit's own spot: only the last point of a queued route gets moved to free ground, so sending a dozen tanks through the same intermediate cell left eleven of them unable to reach it, and the group walked to the first waypoint and stopped there.
- Troop Crawlers wait for their squad to climb back in, up to ten seconds.
- One man who cannot get back aboard no longer parks the Crawler for the rest of the attack move.
- Their squad stays out until the area is clear, instead of piling back in after every kill.
- "Clear" now means as far as the men can shoot, so they finish the enemies in front of them.
- Every man aboard gets out to fight; nobody sits out the battle waiting to be patched up.
- A Crawler that has lost its whole squad carries on with your order instead of idling.
- Attack-move state is saved now, instead of coming back as random memory.

## Your units stop shooting corpses

- Damage counts the moment a shot leaves the barrel, not when it lands.
- A unit says what it is about to do before it does it. Lining up a shot takes time, and a wind-up can run a second on some weapons, and for all of that time there was nothing anywhere saying the target was already being dealt with, so the second and third unit committed to it too and only found out when the first one fired. The shot is now on the books while it is still being aimed. Whoever called it first keeps the target and everyone else looks elsewhere before making the trip, not after arriving.
- Once an enemy is accounted for, the rest of the group retargets.
- An order you gave by hand is held to rather than thrown away, but it holds its fire too. The unit keeps the target and keeps its aim and simply does not spend the round, so the volley is still in the tubes when you point the group at something else. Ordered attacks used to fire regardless, which is why a flight of four planes put all four loads into the first tank it reached.
- A reservation lapses within half a second if the shot never arrives.
- Ctrl+Q now takes infantry only, not every armed thing you own.
- A box drawn with Ctrl held takes those units back out of your selection instead of replacing it, so a group is trimmed by dragging over the corner of it rather than picked again from scratch. Held with Alt the box keeps only what can shoot: drag across your base and you get the tanks, not the dozers and supply trucks standing among them. Both are drag-only, and a single click still means exactly what it always did.
- A box dragged over your army and your economy together takes the army. The dozers, workers, supply trucks and hackers standing among the tanks stay where they are, and ambulances and troop carriers go with the soldiers they look after. A box with nobody military in it takes the workers the way it always did. Legacy keeps the old box.
- The three buttons each do one thing now. Left selects, and only selects. Right gives orders, and nothing it does moves the camera. The camera is on the middle button, and it scrolls the way the right button used to: the spot you pressed on stays put, and the further you pull away from it the faster the map runs. Hold Ctrl and the same drag turns the view instead. The wheel zooms toward whatever the cursor is over and the screen edges still scroll. The classic scheme, where left both selected and commanded and right scrolled and deselected, is gone rather than switched off, and the two checkboxes that used to choose between them are out of the options screen.
- Drag the right button and you draw a line: the selection spreads itself evenly along it, in the order the units already stand, so a column arrives as a firing line instead of a queue. It is the line you actually drew, curves and all, however far you keep drawing it: a long sweep used to give up partway through and finish as one straight run to the cursor, and now it holds its shape to the end. Letting go over the sidebar finishes the order rather than leaving the line stuck to the cursor. Draw the line with the left button while attack move is armed and the whole line is an attack move: artillery spread across a front, each gun firing on what walks into it, instead of one clump on one spot. The left button, because that is what attack move is aimed with; with F armed a right drag draws nothing. Armed with attack instead, every unit fires on its own station along the line, which is a barrage laid across a ridge in one drag. This is what the right button freed up. `FormationDrag = no` in Options.ini turns it off if you would rather a slipped click did nothing.
- Selected units draw a thread to wherever they are going, with the pointer, tinted, sitting on the spot. Every order draws one: move, attack move, attack, force fire, ground fire, enter, dock, repair, heal, capture, hack, guard, a waypoint path. Green for a move, pink for an attack-move, red for anything that shoots, so a group told three different things reads as three different colours. Units standing together and headed the same way share one thread, from the middle of the knot to the middle of where they are going, so twenty tanks sent across the map draw one line instead of twenty laid on top of each other; a selection spread over the map draws one per knot, and a flank sent on its own still shows. An angry mob draws one thread for the whole mob, not one per rioter. An attack move and a shot at the ground both point at the spot you clicked: the attack move's thread used to wander off to whichever free ground each unit was steering for, and ground fire pointed at the middle of the nearest map cell. An attack move stays pink from start to finish, too. A unit that stopped to fight used to swing its thread onto the enemy it was fighting, and only swung it back to your destination once the enemy was dead. A dot and a ring stood in for the pointer for a while, and players asked for the pointer back. It is not a flash at the moment of the order: the line is asked of the unit every frame, so it lasts until the unit gets there, changes its mind or leaves your selection, and it shows on the formation line while you are still drawing it. A unit that has walked off the edge of the screen still draws its line, which is how you see where the half of your army you cannot see is headed. The animated ring the ground used to sprout wherever you clicked is gone: it flashed for a second and told you nothing the marker sitting on the destination does not, and that marker stays for as long as the order does. A marker that has just appeared slides the last few pixels up out of the bottom right and fades in as it lands, over about an eighth of a second, so a fresh order catches your eye without anything flashing. Aircraft draw their thread from the click, not from the moment they leave the runway. A plane parked on its airfield answers an order by starting its takeoff and puts the order itself aside until the wheels are up, so the taxi and the climb, which is exactly the stretch where you are wondering whether the strike went out at all, used to show nothing.
- A unit standing guard draws its marker on the ground it is guarding. A guard order clears the unit's goal on the way in, so every guard marker used to sit in the bottom left corner of the map with a thread running the whole way across it, whatever the unit was actually watching.
- A is attack and F is attack move, each arming the next order click. A click on the ground with A held down is a shot at that ground, on your own tank a shot at your own tank, and the mode drops again the moment the order goes out, so nothing stays armed behind your back. The click keeps your group, too: an attack click that was not a drag used to run the same deselect an ordinary left click runs, so the order arrived a moment after the units it was meant for had been dropped. Both orders are aimed with the left button, the way a special power is, and a right click puts the key down again without giving any order. Holding ctrl no longer forces fire: A is the one way to shoot the ground, and ctrl on a move is only the shared pace. The two keys turn each other off, and the letters they took were sitting on grid commands that moved.
- G is guard, armed the same way and aimed with the same left button. A left click posts the selection on that spot: they hold it, shoot what walks into range and do not chase it off the post you put them on. The cursor turns into the targeting cross while G is armed, so you can see the key is down before you click, and a right click puts it down again. Drag the left button with G armed (the right one draws nothing while G is down) and they post along the line you draw, one unit per station, which is how a hill or a bridge approach gets covered in one gesture instead of a click per tank. Guard has its own colour, blue, on the line while you draw it and on the thread and marker afterwards, so a group told to hold ground reads apart from one told to walk there. Hold position, which posts every unit on the spot it is already standing on, has moved to H.
- With A armed, hold the left button and drag a circle instead: everything hostile you can see inside it becomes a target list, nearest first, and the selection works down that list on its own. When the current target dies the next order goes out the same frame, so a strike group clears a circle without you clicking once per tank. It moves on from a target the group has stopped working on, too, rather than only from a dead one: something none of them can reach or hurt used to sit at the head of the list and hold every other target behind it for the rest of the match. That list is the same queue a shift-clicked attack builds, so you can see it: every target still owed has its own thread and marker, in the order they will be taken, instead of one line to whatever is being shot at this second. Hold shift while you draw the circle and its targets go on the end of what the group is already working through; draw it without shift and the circle replaces the lot. The ground inside the circle is washed the same red while you drag it, painted on the map rather than over it, so it runs up the hills inside the circle and your own tanks stand on top of it. Any order you give by hand ends the list. Losing a unit does not: the list belongs to the group that was told and carries on with whoever is left, which is worth saying because for a while it did not, and one tank dying threw away every target still owed. A target with enough of your fire already in the air to kill it counts as done, so the rest of the group takes the next thing on the list rather than emptying itself into something that is dead and has not fallen over yet. Aircraft are what this is for: a flight fires from range and the missiles are seconds behind, and four planes used to spend all four loads on the first tank in the circle. Running out of ammo does not cost you the list either. A flight that empties its racks flies home, lands, rearms and comes back to the target it left, and the queue waits for it instead of counting the trip as the group giving up: a circle drawn for four planes used to be empty by the time they were back in the air, one entry lost every two seconds they were away. The threads stay on the ground for the whole trip, too. A plane flying home has no target and no order to read, so the list it was still holding used to vanish off the map the moment it turned for base and reappear only when it was shooting again, which looks exactly like the list being thrown away. What the circle picks up is what you can shoot: a shell or a missile crossing it is on the enemy's side like everything else and used to go on the list as a target, costing the group a two second stall on something that was about to stop existing. A target that has driven into the shroud since you picked it keeps its place in the list but stops drawing its thread, so the line cannot be used to trace it through the fog. An attack-move queued behind other orders keeps the force fire it was given rather than losing it when the attack key drops.
- Shift queues an attack now, not only a move. A or F held down while you shift-click adds a force-attack or an attack-move to the line of orders a group is already holding, and it fires the moment the order in front of it is finished - a target dead, or nobody left chasing an attack-move point. The coloured thread that shows a queued order covers it too, so a queue of "go here, then attack that, then attack-move over there" reads as one continuous line in three colours. The game's own white waypoint markings, which used to draw over the same ground the moment shift went down, are gone - they only ever duplicated what the coloured thread already showed, and doing both left a queued attack sitting under two different pictures of itself.
- What the circle picks up is what you can see, and a stealthed unit is not that. Shroud was the only test, so a hidden tank parked in ground you had already cleared went on the list like anything else: drag a circle over open country and the group opened fire on air, one order per hidden unit, which is a stealth detector drawn with the mouse. An undetected unit is not in the circle.
- Ctrl+D takes every unit of the kinds you have selected that is on the screen, and pressed twice inside half a second takes them across the whole map. One tank selected and two keystrokes is your whole armour, wherever it is standing. It was on Shift+Ctrl+E, which is three fingers for something you do in every fight, and it only ever reached across the map when the screen had nothing left to give - so the wide selection happened to you instead of being asked for.

## Aircraft, guards, and orders that used to be ignored

- Two jets on one airfield could deadlock waiting for each other; now they go.
- Losing the airfield no longer leaves its aircraft circling rubble forever.
- Repaired aircraft fly to the rally point instead of hovering over the pad.
- Infantry leaving a captured building follow its rally point too.
- A Chinook unloads its passengers one at a time, not stacked in one frame.
- A guarding unit no longer fights itself between returning to post and shooting back.
- Engineers can clear mines and booby traps they cannot see.
- A helicopter no longer boards a transport from the air.
- A reinforcement joining a team already moving along a waypoint path no longer crashes the match. It picks up the team's current waypoint before trying to follow it.
- Select a big army, right click once, and all of it goes. In a crowd, units step aside to let a neighbour past, and a unit that stepped aside at the moment it was working out its route forgot the order and stood where it was. Out of 120 units sent across a small map, 7 were left behind, and the bigger the selection the more of them stayed. Now 200 out of 200 get there. Clicking on a building did the same thing to the whole group: there was no open ground right under the cursor, so nobody was given a place to stand. The group now stands around the building.
- The whole unit limit moves on one click. The lobby allows 840 units in a match, and the game could only keep 512 of them waiting for a route at once: sent across the map together, 329 of 841 never took a step, then or ever. All 840 go now, whether one player owns them or two armies of 420 are crossing through each other, and the game never falls behind: the click itself costs 13 milliseconds, and no moment of the march took longer than 33, the time the game has for each step of the battle. With all 840 on screen the picture averages 30 frames a second on the new renderer and 37 on the old one, 2.7 milliseconds quicker than before because a crowd of infantry now redraws its shadows' poses in turns instead of every soldier every frame.
- A tank in a column no longer parks behind the one in front while that one drives away. Touching the tank ahead cut its speed a little more every moment and never let it come back, so once the leader had paused, the follower sat at a dead stop until their hulls came apart and then crawled back up to speed. Every pause at the head of a column rippled down it as a line of tanks standing still. The follower now picks its speed up again as soon as the one in front moves off. Across eight maps, stops of a second or more behind the unit ahead fell from 27 to 19 in a 30-unit squad and from 1169 to 781 in a 200-unit army, the longest single stop in that army went from 14.6 seconds to 9, and the last tank of the squad arrives 6.6 seconds sooner. In computer-against-computer matches, the time units spent jammed so hard that the game counted them stuck fell from about 1.3 seconds a match to half a second.
- Tanks and infantry sent together no longer trip over each other. A tank that caught up with a soldier of its own side marching the same way told him to step aside, and a soldier who steps aside forgets his route and stands for a full second before he may ask for another. In a mixed squad of 20 Crusaders and 10 Rangers every one of the 28 second-long stops across eight maps was exactly that. The tank now rolls through a soldier walking its way, the way soldiers already walk through each other; one standing still or crossing its path is still asked to move. Stops of a second or more fell from 48 to 12 in that squad and from 977 to 546 in a 200-unit army, and both arrive sooner: the last Crusader 2.5 seconds earlier, the whole army 1.9 seconds earlier on average. In computer-against-computer matches, time spent wedged fell 11%.
- A jet shot down in the air flies on before it falls. Its wings stopped working on the frame it was hit, so it dropped almost vertically and hit the ground under the spot it was killed at, which is the one thing a plane doing three hundred knots cannot do. It keeps its lift now and gives it up over the next second or so, carrying forward and nosing over as it goes.
- And the bombers come down at all. A B-52, a Spectre gunship, a cargo plane and an A-10 all left the falling speed out of their data, and nought means the lift exactly cancels gravity: shot down, they flew on at cruising height, burning, and blew up in the air still level. Every one of them now falls at half gravity or better, and the fireball waits for the ground instead of going off on a timer somewhere over the map. Measured on the way in: a B-52 falls for 2.8 seconds and lands, where it used to detonate at a hundred feet. On the way home it still did not: after the drop a bomber flies off the map on a precise course that is allowed three times its normal lift, which held a dead one up with lift to spare. A bomber shot down after its run now comes down like one shot down before it.

## The same game on both screens, and after loading

- A unit with nowhere to stand appears where asked, not at a random point.
- Loading a save remembers what your guards were guarding.
- A turret loaded from a save is still tracking what it was tracking, and a worker loaded mid-job carries on with it, instead of both snapping back to their default state.
- A unit that was driving somewhere when you saved is still driving there when you load. One flag was written to the save twice and another not at all, so every loaded unit came back claiming to stand still, and everything that asks - a bike waiting for its rider, a mob following its leader, an ability that breaks if you move - believed it.
- A game saved after losing a supply center loads again. When a truck looked for somewhere to drop its cash and found a center that had been destroyed, it took the center off one list and counted it off the other, so the save recorded the wrong number of supply docks and could not be read back.
- Your units decide the same way on both machines whether to chase what they spotted on their own. That answer was read from uninitialised memory, so the same order could send one player's tank after a target and leave the other's standing.
- A shell already in the air finishes its flight after a load instead of going off in the launcher's face. It keeps the launcher's veterancy bonus too, and a smart bomb keeps homing.
- Supply trucks queued at a dock carry on after a load. Their place in the queue was written to the save and never read back, so every dock stopped letting anyone in until a thirty-second timeout sent the trucks round again.
- A cargo plane or bomber on its way off the map at save time flies on after a load. Its heading was not saved, so the first turn read as a sharp one and the plane was destroyed where it was.
- An EMP pulse, a bike being scuttled, a building's evacuation side and a horde's bonus all come back from a save as they went in.
- Where a supply dock's waiting spots are is the same on every machine. A dock with fewer spots in its model than in its data filled the rest from leftover memory, a different place on each computer.
- A building's toppling bursts are timed the same on every machine. They were timed off the picture's random numbers and created game objects, so two players could see different debris.
- An aircraft carrier remembers the order it was given, and each runway its own ramp.
- A barracks that has already put its mob on the street does not put a second one out after a load.
- Effects tied together - a smoke column and its embers - stay tied together across a save.
- A loaded save puts the world back in the order it was saved in, so the match plays on from where it was instead of resolving everything backwards.
- And it shows you your own memory of the fog, not everyone else's: a loaded game used to draw what every player remembered on top of the world.
- Saved units come back with the map's weapons, not the stock ones.
- Restarting a skirmish restarts the same skirmish, with a replay that plays back.
- Replays sound and look the way they did when you played them.
- Restart keeps the same random colours, starting corners and armies.
- Somebody quitting no longer reports a desync to everyone still playing.
- A power sabotage no longer follows a player into the next match.
- Muting sound effects or speech no longer desyncs the game.
- Pausing no longer leaks a little memory for every sound that was about to start.
- A sound cancelled before it started really is cancelled. The check compared a pointer against a handle - two different things sharing one slot - so it never matched and the sound played anyway.
- Angles come from the game's own table, identical on every machine.
- Defeat, second maps and the sync fingerprint each had a drift bug; fixed.
- The mismatch check now covers whole units and all sixty-eight special powers.
- Order numbering no longer scrambles your commands after about fifteen hundred orders.
- A network game runs at the speed it says it does.
- A frame of orders now travels in one datagram instead of four or five: the packet was still the 476 bytes a 2003 modem could carry, and every extra datagram was another chance to arrive late.
- Input delay is measured from simulation speed now, not your graphics card.
- One lost packet costs a round trip instead of a flat two seconds.
- A desynced match stops at once instead of playing on as two games.
- A single lost packet no longer freezes the match for twenty seconds. The game now asks for the
  missing frame back after a fifth of a second instead of waiting out the disconnect timer, so
  nobody sits in front of a vote screen for a player who never went anywhere.
- A freeze no longer costs everyone two seconds of input delay for the rest of the match: time
  spent stalled is not mistaken for how slow the connection is.
- A player leaving is reported as a player leaving, not a desync.
- A desync now writes a per-object report on both machines for comparison.
- LAN refuses to start between two machines whose game files differ.
- A player whose name holds a comma, a colon or a control character is turned away at the door instead of scrambling the lobby's idea of who is in the room.
- An open seat can be taken. The map's number of starting positions used to overrule the host: a four position map turned away the fifth player even with a seat left open for him, and that seat stayed open on everyone's screen.
- A map sent to you over the network has to be a map. The other machine used to name any file it liked and fill it with anything at all, and it was written where the name pointed; now the name cannot leave the map folder, the kind has to be one a map transfer carries, and the contents have to match the kind.
- The processor's rounding mode is reset from the right register every frame.
- The disconnect screen no longer interrupts a game that is merely slow.
- The keepalive interval setting is read now, and kept in a sane range.
- The anti-freeze brake is measured properly and comes on twice as early.
- Online input delay is now less than half what it was.
- The room climbs back to full speed after one player's brief hiccup.
- Orders are refused unless the sender controls what they name.
- A start-game command mid-match can no longer strand a player in limbo.
- Corrupted-order repair works in the first two seconds of a match too.
- Losing the relay player no longer picks a player who never existed.
- Order confirmations are filed in one step instead of scanning the whole queue.
- A dropped order's retry wait stops doubling after two steps.

## Sharper textures, for free

- The game draws its picture through Direct3D 11 now, and falls back to the old renderer on a machine that cannot make a Direct3D 11 device. Eight views over five maps come out within one step of one colour channel of the old picture, which is the smallest difference a screen can show.
- The Direct3D 11 picture comes with its finishing passes on, no switch needed. Explosions glow past their own edges, because the battlefield is kept brighter than white until the glow is worked out; jagged edges are smoothed; and a light sharpening brings the 2003 textures back up after the smoothing. All three run over the battlefield only, before the health bars and the command bar go on top, so the lettering is untouched. In a screen of thirty inferno cannons firing the frame averaged 11.89ms with them against 11.82ms without. The antialiasing setting on the display page is for the old renderer; `-dx11post off` on the command line gives you the plain picture, and `-dx11post fxaa` picks effects one by one. The old renderer's rolling-wave sea never went missing in a real match either: both of the game's settings files turn it off, so no map ever drew it.
- Fullscreen gets the Direct3D 11 picture too. The old renderer took the whole display for itself and refused the new one its window, so every fullscreen game quietly ran on Direct3D 9, and the DX9 in the corner readout was the only sign. Fullscreen is now your chosen resolution set on the monitor with the game's window over it: alt-tab drops you on the desktop at its own resolution and brightness, and coming back puts the game's on again. A 1280x720 match on a 1920x1080 screen drew 40.7 million times without one draw refused, before and after an alt-tab.
- Alt-tabbing out of a fullscreen match and back no longer leaves every building already on the map as a dark shadow with nothing standing in it. Coming back from the desktop switched the game over to Direct3D 11 halfway through the match, and the new picture only knew about what was built after the switch, so older buildings drew as their shadow alone while a new one beside them looked fine. A match keeps the picture it started with now.
- 481 base-game textures at four times the resolution now beat Zero Hour's downscaled copies.
- A long thin texture loads at the size it was drawn at. Anything wider than eight to one used to be stretched onto a bigger, blurrier one, because that was the limit of a 2002 graphics card; the card is asked now, and modern ones have no such limit.
- Anything standing still keeps its shadow in the fog. A town you have already walked through used
  to sit on flat ground the moment you looked away, buildings and derricks and walls with nothing
  underneath them, because the fog took the shadow off everything. Now only things that move lose
  theirs, which they have to: a shadow crawling across fogged ground is the position of a unit you
  are not meant to see. Ground you have never set foot on is still dark and still empty.
- Woodland stops vanishing when you look at too much of it at once. Every tree on screen was written into one buffer that held about 730 of them, and the ones that did not fit were simply not drawn, so scrolling into a dense corner made whole stands of trees blink out and come back when you left. There are four of those buffers now.

## Every replay, not just the last one

- Replay archiving saves each match under its own date-and-time name.
- Long lists scroll the whole way, past two thousand rows.
- Map, skirmish and replay menus read the catalogue once instead of rebuilding it.
- The replay list's date column shows the date. It only ever held the clock time, so a folder of
  games recorded over months all read as the same handful of afternoons.
- Dates in the replay and save lists are written the way your own Region settings ask for. Both
  lists were formatting against the locale the machine was installed with rather than the one the
  person using it picked, which on any machine set up in one country and used in another is not the
  same thing.
- A save game can be opened straight from the command line: `-loadsave <name>`, the same way
  `-replay` opens a replay. The only route in was the Load menu, so there was no way to put a
  machine back into a known world without clicking through to it.
- A map with an accented character in its name survives the map cache. The name is encoded before it
  goes in, and the encoder stopped dead at the first byte over 127 - so anything past it was simply
  gone, and nothing could read the name back.
- A replay whose header the game cannot read warns you instead of starting anyway. It used to
  report the file as compatible and go straight into playback on something it had already failed to
  parse.

## Placing a building, then changing your mind

- Cancelling a placement no longer deselects the builder.
- The building on your cursor shows where its units will come out.
- An ordered building stands as a see-through plan until a worker starts it.
- Click a plan, or press Stop, to cancel it and take the money back.
- Cancelling a plan is silent â€” nothing was built, so nothing explodes.
- Ground your units have walked stays buildable after they leave it.
- A plan opens no fog of its own, and your opponent never sees it. Not with a unit parked right next
  to the site, and not as a leftover outline in the fog after that unit walks away. Both used to show
  an AI's whole base layout before its first worker arrived.
- And nothing shoots at one. A plan is invisible to the other side, so a tank that stopped and
  opened fire on empty ground was telling that player where your base was going up before a single
  wall existed. The first work a builder puts in ends this and the site is a target like any other.
- A plan does not keep a beaten player in the match. Lose your last building with one plan still
  standing in the fog and the game used to run on forever, waiting for an opponent to destroy
  something they could neither see nor shoot. A plan is not a building; only one a worker has started
  counts.
- Your units walk straight over a plan â€” nothing solid is there until the builder starts work.
- The plan turns solid the instant the first work goes in, so no building goes up inside its own ghost.
- The builder starts work from where it reaches the site instead of shuffling into place first.
- Dozers and workers walk through anything still going up, so they never shut themselves in behind
  their own work.
- The stop button prints its key on it, like the build buttons do.
- A captured worker stops working for its old owner. Being captured cleared its orders but not its
  job, so it carried on building and repairing for the player who just lost it.

## Bonuses that come and go when they should

- Horde, nationalism and fanaticism bonuses now leave with the horde.
- Fanaticism works without nationalism being bought first.
- Two battle plans stack properly, and plans move with a captured strategy center.
- The Bombardment Cannon cannot be fired in the middle of a battle plan change. The turret was only
  switched off once it had finished swinging back, so the whole swing was still a firing window.
- Passengers do not step out of a transport that is itself inside something. They would have
  appeared inside whatever was carrying it.
- A loaded transport or a garrisoned building no longer empties every launcher on the same frame. Five
  rocket soldiers in a Humvee used to put five rockets into a tank at once, before it could back off or
  anyone could react. Soldiers with the same weapon now take turns across their reload: the Humvee still
  fires five rockets a reload, one after another. Rifles keep their own turns and never wait behind the
  rockets.

## Weapons and units that were quietly wrong

- A bunker buster shot down on the way in no longer clears out the building it was aimed at. It emptied the bunker from wherever it happened to be destroyed - a Point Defence Laser did not save you, it just moved the explosion.
- A Battle Bus in its wrecked form takes attack orders again. Every order you gave it was dropped on the way, because the crew inside are 'held' and held units were skipped.
- A sniper cannot pick at an empty Stinger Site. The rule that stops snipers hurting empty buildings only counted passengers, and a Stinger Site's men are spawned, not carried.
- A booby trap shot off a building, or sold with it, releases the building. The mark was only ever cleared when the trap went off, so the building stayed marked for the rest of the match and no second trap could be laid.
- Something flagged unselectable is unselectable. The flag existed and nothing read it.
- A suicide unit pressed up against a building goes off. Touching the building's outline counted as having its view blocked, so it walked away to find a clear line of sight it never needed, came back, and did it again without ever detonating.
- A nuclear missile hurts the whole of a big building. The blast measured to the middle of everything near it, so an airfield whose centre sat just outside the outer ring took nothing while half of it stood in the fire. Distance is now taken halfway between the nearest edge and the centre.
- Rockets curve into their target instead of snapping onto it. In the last few frames before impact a guided missile swung its nose straight at the target in one step, up to 40 degrees in a single frame, when the whole flight before that never turned more than 5. It now flies an arc from the heading it had to the spot where it meets the target, so a tank driving across its path is led rather than chased. In the same test battle no rocket turns more than 8 degrees in a frame, and all 101 land. Rockets fired at aircraft still fly straight at them: a helicopter is nearly as fast as the rocket, and on an arc the rocket flew past it and climbed away into the sky.
- Scuds come down from the top of their climb in a smooth curve, level at the top and steepest at the end, instead of tipping into a straight dive 200 short of the target. Over those last 200 a Scud now stays within 14 of that curve where the dive strayed by up to 49, and every one lands on the spot it was fired at; one in the old test came down 22 away.
- Cancelling an order of Red Guard after the first soldier has walked out no longer refunds the order. One order is two soldiers for one price, and the refund came back whole, so the first one was free.
- Double-clicking to grab everything of a kind grabs what a box would, not things you can only click one at a time.
- A bounty pays what the percentage says. Rounding up a fraction that was already a whole number paid a dollar extra on every kill.
- A supply upgrade pays for what arrives. The bonus was a flat sum handed over on arrival, so a truck turning up with one box collected the same as a full load - and a driver dropping off little and often earned several times what the upgrade is worth.
- Taking over a defeated ally's base no longer starts his research again from scratch. A player upgrade is bought once for the whole player, and the buildings you inherit were left paying for a second copy of what you already had.
- A tank drives out the moment it is paid for. The factory doors only began opening once the vehicle was already finished, and it stood inside waiting for them, so every single unit cost its build time plus the whole door animation on top - on every tank, all game. The doors now start moving during the last seconds of the build and are open on the frame the unit is done. And they stay open while there is another vehicle behind him, however long that takes: a factory working through a queue keeps its doors up and shuts them when the queue runs out, instead of dropping and hauling them back for every unit. That was worth watching in its own right - there is no artwork for a door that changes its mind partway, so a door caught mid-close and pulled back open snapped wide in a single frame.
- A unit lifts the fog only half as far again as its guns reach: enough to spot what it is about to fight, not enough to scout. A rifleman used to see well past the range of his rifle, so any cheap soldier was a scout. Artillery that outshoots its own eyes still needs a spotter, and units with no weapon at all, like bulldozers and supply trucks, keep the sight they had. Buildings are unchanged.
- Hills and buildings block sight. A tank at the foot of a ridge used to see straight through it to the far side, and a squad in a city saw through every block around it. Now the fog stays on whatever a ridge, a building or a tree hides, yours, the enemy's or a civilian's, and a unit on high ground looks down over everything below it. Aircraft fly above it all and see past. Soldiers garrisoned in a building still see out of it; its own walls do not blind them.
- High ground pays. A tank on a ridge now shoots, sees and picks its targets further down the slope than it could on the flat: three steps of range for every step of height over its target, up to twice its usual reach. Firing uphill costs nothing, and aircraft get no bonus from flying. Defences count too, and the range ring you see while placing one bends outward wherever the ground falls away, so you can site a Gattling Cannon on a hill and see exactly what it covers.
- The five soldiers in an Overlord's battle bunker live through the Overlord's death again. A rule added in this build, that nobody gets out of a vehicle which is itself being carried, caught the bunker too, so all five died on the spot where they used to take half damage and climb out.
- Pro Rules hold for the nuke and the carpet bomb whoever fires them. The refusal stopped the recharge but not the missile: a computer player or a map script could launch a neutron missile the rules forbid, and because the timer never started, launch it again straight away.
- A Spectre gunship called in by the computer or a map script circles its target. Its aim point and its gun's aim point were only set for a gunship you called yourself, so theirs lined up on the corner of the map and walked across from there before the howitzer could fire.
- A campaign's scripted drop from a B-52 or a cargo plane already in the air uses that plane. The instruction to leave the plane alone reached the game as a lifetime of zero, and a second transport was made at the drop point.
- A radar upgrade finished while its building was knocked out counted twice, and you kept the radar after the building was gone. It counts once.
- A unit built to survive on one hit point survives armour too. The cap was applied to the raw shot, before armour multiplied it, so anything weak to the weapon still died. The Battle Bus had it worse: the killing blow ran its death, then its second life revived it anyway.
- A building cleared by a Neutron Shell forgets the stealthed GLA soldiers it held. It kept counting them, so the Rangers who moved in next could be walked in on by an enemy and thrown out.
- A Point Defence Laser leads a fast missile the way its data says it should. The predicted position was worked out and then the distance was measured to where the missile already was. It also stopped firing one last shot at a missile that had just left its range.
- An Angry Mob bought at a barracks walks out one at a time, as the stagger in its data asks. The delay between members was calculated and thrown away, so the whole mob appeared at the door at once. A mob that loses its leader can take back the stragglers, which nothing could do before because every name in the list was skipped.
- A missile fooled by flares goes for the nearest flare of the last volley. It weighed the newest flare several times over and never looked at the others.
- A toppling building crushes the strip across its fall at any angle. The line was only square to the fall when the building fell along an axis; at forty-five degrees it ran down the length of the building and missed the units beside it.
- A transport killed by the blast of a Terrorist riding in it puts the other passengers out. They were handed back to a vehicle that had already died and were deleted with it, unseen.
- Something that knocks a unit off its feet lets it stand again. The flailing pose could outlast the stun and stay for good.

## One crate, one collector

- Salvage gets collected. A wreck leaves money and a free upgrade lying on the ground, and the game asked you to spot it in the middle of the fight that made it, work out which of your units was allowed to take it, and drive that one over it by hand. Nobody does that, so most salvage on most maps timed out where it fell. Whoever is standing nearest with nothing to do goes and gets it now: a unit that can still be upgraded off it goes first, however far back it is standing, and failing that the nearest idle unit takes the cash. Only idle units, so nothing is ever pulled out of a fight or off an order, and dozers and harvesters are left to the job they are already earning at.
- And anybody can take the money. A salvage crate could only be picked up by a unit that salvages, so one dropped among troops who could not use it was money that belonged to nobody. The parts still only go to a salvager - the weapon, the armour, the promotion - but the cash goes to whoever drives over it.
- A crate pays once per frame, not once per soldier who touched it.
- A unit that kills something carrying a crate now goes for the crate. The game noted the crate, then forgot which one before it looked, so the "pick up what I just dropped" order at seven places in the code never once fired.
- A thrown vehicle's crash damage hits the pile once, not once per unit.
- A dying unit is no longer promoted by its last kill.
- Healing no longer counts as damage, so a medic stops revealing its stealth unit.

## Circles are round, and a tunnel is one tunnel

- Circular range checks are circles now, not the square drawn around them.
- A tunnel network heals at one rate, however many entrances it has.

## The power bar tells the truth

- An EMP on an upgraded plant no longer takes the upgrade off your grid twice.
- An EMP on a building site no longer moves power that does not exist yet.
- Loading a saved game keeps disabled power plants disabled.
- Control Rods finishing during a blackout is no longer credited twice.

## Your whole base, on one strip

- One row above the command bar carries everything your base is building anywhere.
- Ordered by time left, so it reads as the order things arrive.
- Select a building and what it is making moves to the front of that row, ahead of everything else, still soonest first among itself. It used to get a whole second row of its own under the base's, which said the same thing twice and pushed the strip further up the screen; now the head of the strip is the building you are looking at and the tail is the rest of your base.
- Click a picture to jump the camera there; Ctrl-click cancels it.
- One picture is one order, cut to match the command bar's artwork.
- The strip is half the size it was: it says the same thing and takes back the screen it was eating.
- Whatever is actually being built counts down inside its own picture. The ones queued behind it stay blank, because their wait depends on everything in front of them.
- A unit finishes walking out of its factory before it takes an order.
- An upgrade stays available if any selected building can still buy it.
- One right click cancels one thing, counted the moment you press.
- A build button takes a batch: Shift queues five, Ctrl twenty, both together as many as the queue will hold and the bank will pay for. Right-click with the same keys and the same number comes back out. Ten tanks used to be ten clicks.
- Right-click a picture on the strip and that one order is cancelled and paid back, without walking the camera over to the building first. A build button greyed out because the bank is empty or the queue is full still takes the right click, so what is already queued on it can come back out.
- Buying an upgrade for a group starts it in all of them.
- The progress clock over a picture is steady at any frame rate.
- A group's upgrade shows its progress, whichever building is paying for it.
- Click a unit with several factories selected and it takes the shortest queue.
- A plane stays buildable while any selected airfield still has a free spot. One full airfield used to grey the button out for the whole group, even with three empty runways in the same selection.
- A general's upgrade spreads across the selected buildings instead of hitting one.
- A clicked upgrade darkens immediately and uncovers as it progresses.
- The production strip dims fresh orders the same way.
- A second worker can take over a half-built building.
- The nearest idle worker takes a new job, without yanking a busy one off.
- A worker that is building something still offers you the whole build list.
- The GLA's decoy build list toggles both ways now.
- Workers go back to collecting supplies when they finish.
- Workers and supply trucks walk to a pile up to 1400 feet away, the reach the computer opponent always had. Yours gave up at 700: when the pile beside your base ran dry they went home and stood there, with the next pile in plain sight.
- Every countdown reads real seconds and answers the game speed.
- The clock and frame-rate readout is drawn on top of everything.
- The superweapon countdowns are pictures now, not a list of names: the same strip treatment, in the top right corner, under that readout. Each one wears its own seconds and a sweeping clock, the next to land sits at the right hand end, and if you are watching more than eighteen of them the rest become a count.
- Every button's corner markings - the hotkey, the price, the time, the number queued - sit flush in the corners instead of floating a few pixels inside them, and are set a size smaller than the button's own text: four labels at full size were eating the picture they were meant to annotate.
- The messages in the top left corner - out of money, building captured, a unit lost - are set a size smaller, the same notch the superweapon countdowns were taken down by. They are notes on the battlefield, not part of the panel, and at full size a run of them was climbing over the fight they were reporting.
- A superweapon's wait is written in plain seconds - 200, not 3:20 - so it compares with every other countdown on the screen at a glance. Once it is charged the number goes away entirely and the picture itself breathes in your colour, which is the one thing you want to catch out of the corner of your eye.
- The countdown inside a strip picture moved down into its bottom left corner and carries the unit with it - 45s, 200s - on a translucent plate. In the middle of the picture it sat on the one thing the picture is there to show, and the plate it lost when it was in the middle earns its place down in the corner, where a pale cameo was swallowing the digits and their shadow together. This applies to the superweapon countdowns in the top right as well.
- Every picture in the row stands in a tray - the same tray your general's powers stand in down in the corner, your own side's copy of it, so the American, Chinese and GLA strips each carry their own metal. The tray is turned back to front, because that bar grows out of the right hand corner and the strip grows out of the left, so its heavy rail leads the row instead of standing between every pair of pictures. It is never stretched to fit, and side by side the trays close up behind each other by exactly what that bar closes its own up by, so a row of them reads as one run of metal rather than a line of loose boxes. Stacked they stand clear of each other instead: a picture in a column carries its whole tray, with none of the one above it cutting across its top edge. The strip is flush against the left edge, and a black rectangle laid over the battlefield is gone.
- The queue stands up out of the corner instead of lying across the bottom of the screen. It is a column now, five pictures high, growing upward from just above the command bar with the next thing to arrive at the bottom of it - so the one picture you actually read is always in the same place, a thumb's width above the buttons, and it does not move when six more tanks are ordered. What is left over closes the column as a sixth cell wearing a "+N". A busy base used to lay its whole queue across the battlefield; now it costs one picture's width of screen, whatever it has coming. Watching a match it goes back to rows, because there the up and down belongs to the players: one row each, piled off the bottom left corner, each row reading left to right.
- A slot in it is a slot of that bar to the pixel, at every resolution - the size is measured off the bar itself rather than guessed - so the queue is read at a glance instead of squinted at: these were postage stamps a third of that size. Ten tanks ordered back to back are one picture with an x10 in its corner rather than ten copies of the same tank eating the row, so what is left of the row still says what else is coming.
- The buildings your workers are raising stand in that same column, not in one of their own: a war factory going up is a picture in the queue at the point its time puts it, between the tank that lands before it and the one that lands after. One column is the whole answer to what your base has coming, read bottom to top in the order it arrives, and it no longer takes a second strip of screen to say the slower half of it. Each site carries its own sweep and the seconds it still needs. Click one and the camera goes there - a half-finished building under fire on the far side of the map is one click away instead of a hunt across the minimap - and none of them can be cancelled by mistake: a building already standing is sold or lost, never cancelled, so Ctrl over it does nothing.
- The superweapon strip in the top right stands in the same metal, the right way round this time: it is anchored to the right hand edge and grows leftwards, which is the way that bar in the corner grows, so its rail closes the row against the edge of the screen. The black rectangle behind those pictures is gone with it, and the countdowns are the size of the ones down in the corner rather than half of it.
- Nothing in a row of trays lies over the picture in the tray beside it any more. A slot used to slide an eighth of a tray back under its neighbour so a row would read as one run of metal, and what it actually did was drop that neighbour's rail across the left edge of every cameo but the last - visible on the general's powers along the bottom right and on the superweapon countdowns along the top. They step a whole tray now, sideways as well as upward, which is what the queue column down the left has always done.
- A game opens with nothing selected, so your first click is not a rally point.
- Your general's powers no longer climb the right hand edge of the screen. They sit three abreast, filling from the bottom corner where the first one has always been and wrapping upward, so a general holding eleven of them still reads as a block instead of a ladder from the radar to the sky. The keys follow the same shape: the first press picks a row, the second picks the power in it, so every power is two keys away - F1 F1 for the one in the corner, F2 F1 for the row above it. Only the keys you can press next are written on the pictures, so you are never reading eleven labels to find one, and Escape drops a row you thought better of - so does clicking a power with the mouse, and so does four seconds' pause, so a key hit by mistake in a fight never turns your next one into a superweapon. Rows with nothing in them yet cannot be picked at all: early on, when you hold three powers, only the row you have answers a key.
- The second key of that pair works now. Anything that touched the interface at all - a promotion, a unit rolling out, a building captured somewhere across the map - was quietly forgetting the row you had just picked, and in a real fight that happens several times a second, so the pair almost never completed and the powers looked as though they answered no key at all.
- The key written on a power is drawn over its cooldown sweep instead of under it. A power still charging had its label buried by the black wedge sweeping across the picture, which is exactly when you want to know which key it is.
- The tilde key opens the general's promotions, and Escape closes them - Escape used to bring up the pause menu on top of the open screen, leaving the small exit button in its corner as the only way out. The screen is five columns wide and 1 to 5 now name them: each number sits in the corner of the science that column would sell you next, and pressing it twice buys that one - once to mark, once to spend, so a point is never gone to a key hit in a hurry. Buy the top of a chain and the number drops onto the next rung down, so a whole column goes in with one finger while the fight carries on behind you.
- `S` stops your units again; the key was simply never bound.
- Every button carries its build, research or cooldown time in the corner.
- A single selected unit wears a gold bar filling toward its next rank.
- Timings everywhere: buildings, queues, superweapon charge, upgrades being researched.
- A charge bar now says how many seconds are left.
- Aircraft always show how many attack runs they have left.
- The corner readout separates game time from real time, and sim rate from fps. It also says DX11 or DX9 after the frame rate, which is what your machine is actually drawing with: a graphics card that cannot start Direct3D 11 falls back to 9 without a word, and a frame rate means nothing until you know which of the two it belongs to.
- Pausing stops both clocks.
- Watching a match, the strip becomes every player's queue at once: one row each, bordered in that player's colour, showing the three that land soonest and a count of the rest. Buildings going up on the ground are in those rows too, so a player answering an attack with three war factories is visible while the concrete is still wet, and eight players fit on one screen because a row is three pictures wide.
- Every row and every countdown stands in real metal, cut from the same tray the general's powers sit in down in the corner - the side your command bar is showing. Watching a match there is no bar of your own, so both strips used to fall back to a flat black box for the whole match; now they wear the metal of the player you are watching, and follow it when you switch seats.
- Watching, the bars over the buildings are everyone's too - what each factory is turning out, and how long the superweapons have left. An observer used to see none of it.
- Watching, you see the fog the player you are watching sees. The game only ever remembered one player's fog, so switching seats handed you the first player's idea of the map.
- An ally, and anyone watching, now sees a stealthed unit's muzzle flash, its promotion and the cash it earns - if they can see the unit at all. Those three asked 'is it mine' instead of 'can I see it', so allies and observers got nothing.
- Watching, the match opens on the middle of the map. An observer has no start position, so the game looked for the camera waypoint of "player zero", did not find it, and fell back to the corner of the map: every match watched from the stands began pointed at empty ground in the south west.
- Watching, you can click a stealth building. The rule that stops you selecting an enemy you cannot see never asked who was asking, so an observer, and a player already knocked out, could see a stealthed structure on the map and not pick it up to read it. A spectator commands nothing and sees everything already, so there is nothing there to give away.
- Watching, the promotions every general has bought are on screen the whole match: a strip of them in the bottom right corner, standing on the command bar the way the production rows do on the left, one row per player in his own colour. There was nowhere else to find this out - the promotion screen filled itself in with the watcher's own player, who has no general and no command sets at all, so a spectator got a blank screen.
- Watching, clicking one player's unit is clicking his name in the player list, and clicking empty ground puts the whole match back. The command bar brings up his readouts and his money and wears his side's metal; the production rows on the left close down to his, and get longer since he has the strip to himself; the promotions on the right are his alone. The tilde key, or the general's stars on the right of the bar, then opens his promotion screen, on his side's artwork, with his skill points and his rank - greyed out, because they are not yours to spend. With nothing selected the key does nothing and the stars are dark; there is no one general to show.
- A general who has taken Artillery Barrage to level 3 shows one Artillery Barrage on the promotions strip, not three copies of the same picture in a row.
- The strips close up again: across a row the trays slide an eighth of a tray under each other, so a row reads as one run of metal. The cameos never overlap each other, because every tray goes down before any picture does.
- Watching, the strips can be switched off. A small "Strips" drop-down in the top left corner opens into three boxes - the production queue, the generals' promotions and the superweapon timers - and each box switches its strip the moment it is clicked and is remembered for the next match you watch. The message list moves down to start under it. Playing, there is no drop-down and every strip is up, whatever you chose in the stands.
- A minimised command bar stays minimised. Changing seats, or clicking another player's unit while watching, put the whole bar back up over the battlefield you had just cleared. A new match still opens with the bar up.

## Placing buildings

- The placement grid is drawn on the ground and follows the slope, and reaches twice as far as it used to.
- Unbuildable ground is a soft red wash, crossed out square by square.
- Running out of money no longer strands the ghost on the map.
- Holding shift and clicking out a row of buildings no longer stops halfway and selects one.
- That row keeps going with no worker selected, too, instead of ending after the first building.
- Turning a building no longer turns everything you build afterwards. A wheeled heading still carries from one wall to the next, but the next supply centre comes out facing the way it was designed to.
- A building you point at blocked ground slides to the nearest spot it fits, and lands there.
- The pointer keeps its build cursor while you place, even passing over your own buildings.
- GLA defences get a range ring while you site them.
- The ground a defence on your cursor can shoot into is painted red. It used to be the thin white guard circle, which read as the edge of a building's footprint more than as a field of fire; now it is a thin line in the owner's colour, and the line sits exactly where the game stops allowing the shot: the weapon's range measured from the edge of the building, not from its middle. While any building is on your cursor, or when you click a turret, every armed building you can see shows its reach as well, yours, your allies' and the enemy's, and one player's overlapping circles join into one outline, so the gap between two Patriot batteries is something you can see before you pay for the third. An enemy defence hidden under fog stays hidden.
- Inside that red area, the ground the defence cannot see is shaded. A Patriot battery, a Gattling Cannon or a Fire Base does not shoot through a building - the game refuses the shot when a solid structure stands between the gun and its target - so from where your cursor is, each building in range throws a shadow that starts at its near wall and runs out to the edge of the reach, a wedge that swings as you move the defence round the corner of your War Factory. Hills count as well. The game will not pick a target when the ground rises between the top of the gun and the top of a tank, so the far side of a ridge goes dark too, and a valley beyond it lights up again where the gun can see down into it. The shade lies on the ground as one piece and follows its slopes. A corner one of your turrets, or an ally's, already sees into is not a hole, so it stays bright: the shade is only what nothing of yours can shoot. A Stinger Site or a bunker gets no shade, because a building in the way does not stop them. Click a defence that is already standing and it shows the same shade from where it stands, its own and nobody else's, so the corner behind your War Factory is there to see after the turret is built too.
- Clicking a turret in a base full of them no longer drags the game down. Every circle's outline was worked out again against every other circle on every frame, so with 120 Patriot batteries on the map one selected battery took the game from 94 frames a second to 53. The outlines are worked out once and kept until a building goes up, comes down or comes out of the fog: the same base now holds 83 with the battery selected against 84 with nothing selected.
- Selecting a GLA supply stash no longer puts reach circles round every defence on the map. The stash spawns workers, a worker carries a weapon for clearing mines, and that was enough to count the stash as armed.
- Two buildings can no longer be put down on the same spot online. A build order travels to the other players before anything appears on the ground, and every click made while it was in flight was answered by a map that still showed the square as empty - so a shift-held row on a bad connection came out as buildings standing inside each other. The ghost now knows about the orders you have already spent, and turns red over them or slides to the square beside; and the order itself is checked again where it lands, on every machine, after the first building is standing. A second one aimed at the same ground is refused there and costs you nothing.
- The right button drops the building on your cursor whether the click travelled or not. Sending a dozer off somewhere in the middle of a shift-held row moved it and left the ghost riding the cursor, because the cancel only counted a press that had not moved a pixel and a hand on the way to a destination always moves a few.
- A click clears a half-typed building shortcut.
- The two-key building shortcut survives the command bar's redraw now.
- Every structure is those two keys and nothing else. The letter written on a cell is the second half of its pair, and pressed on its own it used to build whatever sat in the slot that letter names - so the same barracks went up either by the shortcut painted on it or by one bare key nobody had written anywhere, and a key pressed after a shortcut you had thought better of put a building on your cursor. And the pairs are read off the key bindings now instead of a list written out beside them: the second letters had been left behind when the bottom row of the command bar moved to Z X C V B N M, so half the cells answered a letter that was not on them.
- The command bar is driven by its own grid of keys - the buttons in the shape they are in, under your left hand - and there is no longer a switch for it. It shipped off by default, which meant nobody ever saw it.
- W A S D can move the camera. It is a box on the Controls page, off until you tick it, and it only takes with the Modern input scheme: pick Legacy and the box goes grey. On top of the arrow keys and everything the mouse already does, and it moves the keys a fight is fought with one row down to meet the hand: F attacks, G stops, H attack-moves, J guards, K holds position. The grid's top row steps over W and reads Q E R T Y U I, which pushes the idle-worker key from I to O, and every letter painted on the command bar follows the moment you press Accept. Let go of a camera key with Ctrl held for a control group and the camera still stops. The pointer stays the pointer while the keys scroll, instead of turning into the scroll arrows the mouse uses.
- Escape cancels what you were doing before it opens the menu.

## Where the money is, and where it is coming from

- Supply piles and docks say what is left in them, in cash, over the pile. No more guessing which expansion is worth taking from the art on the model.
- Every build button carries its price in the top right corner, opposite the build time already in the other one. It was only ever in the tooltip, which means hovering one button at a time to compare two of them.
- A structure's button says what it does to the power grid, in the fourth corner: what it draws, or what a plant puts back. Money and time were already on the button and power is the third thing a base spends; it was the one figure you had to hover for, which is the wrong way round for the building you are putting up because the lights went out.
- A worker fetching or handing over a box shows a bar while it works.
- That bar is now the work itself. A worker used to reset it to empty at the very moment it finished and then walk away, so every trip looked like it left mid-handover; and after taking its last box it stood at the pile for one more full loading cycle, taking nothing. It leaves the instant the load is done - which is a little more money per trip, on every worker you own.
- A worker loads in one go instead of a box at a time. It used to take one, wait a loading cycle, take the next, wait again, and the bar over its head restarted for each of them; now the whole load changes hands once and the wait is that load's worth of time - four boxes cost what four boxes always cost, in one stretch rather than four. Money in the bank is unchanged, which is the point: three headless matches on the same seeds came back within a few percent of what they earned before.
- Supply points refill themselves, slowly, and faster the more of a base has grown around them. A warehouse used to be a fixed lump of money that ran out and left the map with nothing to fight over: whoever mined out the middle first had the game, and the second half of every match was played on income neither side could earn any more. Six hundred dollars comes back every forty seconds standing on its own, every twenty with one supply centre built near it, every thirteen with two - up to what the point started with, never past it. A single box was too little to be worth the trip: a hundred dollars every forty seconds is less than the harvester earns going to fetch it, so a mined-out point stayed mined out as far as anybody playing was concerned. Both sides expanding onto the same patch make it richer and then have to decide which of them keeps it.
- Your workers tell you a supply point has run dry once a match. With piles refilling, they ran dry again and again, and every time the same worker said so.
- Workers stop hesitating on their way in and out. A dock has a queue outside it and a worker treated its place in that queue as a destination: it drove to the spot, stopped dead, waited a frame, drove to the door, stopped again, and only then went in - twice a load, and nearly every load with nothing else queued. With the dock empty it drives straight in. The first version of that also skipped the walk to a supply pile or a GLA stash, which have no door of their own, so a worker a long way off played its loading animation where it stood and was paid anyway. In a four-player test match 72 loads were picked up or dropped off away from the dock, one of them 613 feet from it; now there are none.
- Hackers show how far off the next payout is. So do the black market and the oil derricks.
- A hacker working from inside an Internet Center shows what it earns. The green figure floats over the building on every payout, the way it does over one sitting in the field. It never appeared, because the game asked the hacker whether it could be seen and a garrisoned unit is not drawn at all - which is the one place a hacker is meant to work.
- Factories take a hundred units in the queue, not nine. The build queue only ever had nine buttons and that had become the limit.
- A match can pay everyone a wage. `MoneyPerMinute` in `GameData.ini` drops the same sum into every
  living player's account on the minute, on top of whatever they are mining, which is the knob a
  mod or a map needs to run a game that is not about holding the supply piles. Zero, and therefore
  absent, unless somebody asks for it: on a stock install nothing changes. Measured on one seed with
  four brutal computers at 7,777 a minute, three of the four spent between 3,400 and 6,200 more in
  the first three minutes and put the difference on the map.

- The minimap says where the money and the capturable buildings are, with the dollar and the star from the lobby map preview. Oil derricks, hospitals and repair pads never showed on it at all, and supply piles vanished with the rest of the map under the fog, so the two things worth walking a rifleman across the map for were the two things the minimap would not tell you about. They are marked from the first second now, on ground you have scouted and ground you have not, one mark per place rather than one per pile; a pile that runs dry drops off it.

## Health bars

- You pick who wears one. `HealthBars` in `Options.ini` takes 0 for everyone, 1 for smart, 2 for the selection only and 3 for nobody. Smart is the one worth trying: a bar over a unit at full health tells you what its absence would have told you, so healthy units go bare and anything that has been hit stays marked, with whatever you have selected or are pointing at readable either way. 2 is what the original game did. The default is 0, so nothing changes until you change it.
- A health bar keeps its size against the unit at any resolution. It was a fixed number of pixels wide and three tall, so the bigger the screen the thinner the thread over a tank.
- Health bars are in the owner's colour instead of green to red. A building going up fills its bar in that same colour as it rises, so a glance across the map says whose expansion it is - the seconds written over it already say it is not finished. A disabled building keeps the blue.
- A building's bar sits above its roof rather than inside its art.
- A unit that is inside a transport and also disabled wears the blue bar. Being carried and being knocked out were asked as one question, so the two together answered no: a squad EMP'd inside a bunker, or hacked while riding a Battle Bus, looked perfectly healthy from outside.
- A building being captured wears a capture clock under its health bar, filling in the colour of whoever is taking it. The building flashes while a rifleman works on it, which says somebody is at the door and nothing about whether that is nearly over: the choice between driving across to shoot him and carrying on with your fight was made blind. The bar runs from the moment he reaches the door to the moment the building changes hands, and Black Lotus hacking one gets the same bar.
- The progress bar stacked over the health bar is white, wherever it turns up - a factory turning out a tank, a silo charging, a supply drop counting down to its payout. It was yellow, which is a colour the game already spends on your own units and on damage, and a strip of it sitting directly above a coloured health bar read as part of the same bar.
- Every garrisonable building shows how full it is, whoever holds it.
- A vehicle's load stays private.
- What you have selected wears a white frame just outside its bar, so a selection is still readable in a crowd where every unit carries a bar - and the bar itself keeps its owner colour all the way round.
- Lamps, barrels, rocks and bushes no longer wear health bars.
- Bridges no longer float a health bar over the middle of the river. There was never anything under it: the span is part of the terrain, and the bar belonged to an invisible marker standing in the water.
- The bar is part of the unit now: click it and you select whoever it belongs to. Zoomed out, an infantryman is a few pixels of helmet under a bar that is far easier to hit, and a unit half behind a building still has its bar in the clear. It only answers clicks that would have missed everything anyway, so it never takes a click away from the unit you were actually pointing at, and right-clicking bare ground under a bar still orders a move. And it stands down the moment you have something selected: with a force in hand your clicks are aimed at the ground, and a bar hanging over that ground used to cost you the whole selection and hand you back a single passer-by.
- The city itself stops wearing them too. A building nobody owns shows a bar only if there is something to do about it - troops can go inside, or it can be taken. A row of houses does not, and neither does the concrete apron each of them stands on, which is a separate two-thousand-point object that was drawing a second bar down at street level.

## Twenty-two-year-old bugs, found by testing

- Pressing Continue after a campaign mission goes straight to the next one. The main menu used to wake up behind the score screen on the way out, show its buttons and start loading its own background battle, so the menu sat on screen over the next mission's load.
- A civilian car no longer drives into your base in the first minute of every game. A map's civilians are owned by a computer player like any other, and the scouting pass looks for anything of that player's that can drive - so it was picking a parked car out of the scenery and sending it to a start position. A side with no faction and no build list has nothing to learn from the map and does not scout.
- A rifleman pulled off an oil derrick mid-capture left it flashing and chiming for the rest of the match, and the derrick changed hands anyway with nobody standing on it. Walking away now stops the capture, whether you or the computer gave the order.
- Every countdown on screen was a second too long. Rounding up a whole number of seconds gave a whole number plus one, so a ten second build said eleven - production queues, buildings going up, superweapons, all of it.
- A Chinese silo researching an upgrade while its missile charged drew both bars and both countdowns in the same row of pixels. They stack now.
- Units moving diagonally ran up to 40% faster than their own stat sheet.
- Garrisoned infantry and base defences only ever range-checked one of their weapons.
- A Gattling Cannon, a Patriot battery or a Stinger Site no longer locks onto a tank it cannot reach. Each has a short-range ground gun (225) and a longer anti-air one (350 to 400), and the air gun's reach counted for ground targets: a tank at 300 became the target, the turret swung after it, nothing fired, and a helicopter overhead that the air gun could have hit went untouched until the tank left or died.
- Patriot batteries aim at the aircraft they shoot at. The lift they add to lob a missile over a ground target was added to every target, so against a helicopter the launcher pointed up to 40 degrees above it.
- An EMP or a Hacker no longer leaves a Bunker or a Fire Base shooting. The blast switched the building off and left the soldiers inside alone, and they kept firing out of it for as long as it was dark.
- Soldiers in a Bunker fire at the edge of their range. One already standing at a firing slot measured his reach from the other, empty slots, so at the edge he let go of a target he could hit, picked it up again and never fired. Your own Bunkers measured from the middle of the building and ignored enemies at the edge altogether, where a computer player's did not.
- A Stinger Site's soldiers stay at the site. A computer player's soldier followed a reversing tank across the map and stood wherever it died, never to return, and yours ran out several hundred paces to hit back at whoever had shot a neighbour.
- A computer player's GLA tunnel no longer empties under a helicopter. A strafing Comanche made itself the tunnel's target, and the rebels inside poured out after an attacker none of them could shoot.
- An EMPed or unpowered defence stops acting as if it were fighting. A Gattling Cannon hit mid-burst spun its barrels for the whole blackout, and one ordered to attack while it had no power spun up without firing a shot. It drops its target now, can't be given a new one until it is back, and picks one again when it is.
- A vehicle knocked out by an EMP while its turret was turning goes quiet. The turret's motor sound only ever stopped when the turret next moved, so it looped for the whole time the vehicle sat dead.
- A Fire Base can be ordered to attack what only the soldiers inside it can hit. The base's own howitzer answered first, and a howitzer hits nothing in the air and nothing within 50 of the base, so Missile Defenders inside got no attack cursor on a Comanche.
- A defence that picked its own target moves on once that target is as good as dead. When the shots already in the air were enough to finish it, a Gattling Cannon held its fire and kept aiming at it, for up to three and a half seconds, while the next tank drove up. It lets go and takes the next one.
- The green beam between Patriot batteries means a battery is really joining in. A shot called every idle battery within 200 to fire a volley at the same target, and the beams went out to batteries that could not: one without power, one knocked out by an EMP, one already busy with its own target, one the target was too far from. None of them fired. A battery shooting down a missile called the rest in too, with a weapon that cannot hit missiles. Only a battery able to hit the target from where it stands is called now. A battery whose target died after one missile of its volley also kept the longer assist range for its own shots until it next emptied a volley; it drops it when the target goes.
- Killing something with poison or toxin credited nobody with experience or score.
- Healing a unit counted as attacking it, so guards chased the wrong target.
- A supply centre could stop accepting trucks permanently.
- Selling a building mid-research gave no refund and left the upgrade in limbo.
- On-screen messages now hold for two and a half seconds and fade in one.
- Closing a dialog while another one sits on top of it no longer leaves the game holding a pointer to the window it just freed. Every click, every keypress and the tab order all read that list, so what it caused was a crash with no pattern to it.
- A dialog asked to open twice closes once. It went on the list of windows that take every click a second time, closing took one copy off, and the other kept eating input for a window nobody could see.
- Ordering a group to use a special power that can kill the units casting it, the GLA rebel ambush over water being the one that does it every time, no longer walks a list of units that the ambush is deleting underneath.
- A replay that never recorded which seat was yours plays instead of crashing before the map loads.
- Alt-tabbing back into the game no longer leaves you in waypoint mode. Alt is the key you hold to lay a route and it is also the key you hold to alt-tab, so the release went to Windows and the game never heard it: every click after that queued another waypoint. Ctrl, shift and the keypad camera keys were the same story.
- Alt-tabbing away with an arrow key held no longer leaves the camera sliding that way when you come back. The key release goes to whatever you switched to, so the game never heard it; losing focus now ends the scroll, the middle-button pan and the ALT pitch drag along with it.
- Locking Windows mid-battle no longer eats the game. Nothing that renders runs while the screen is locked, and the particle bookkeeping had been filed under rendering, so every explosion while you were away was created and never cleared up. A hundred rocket buggies firing at the ground for a minute is enough to come back to a game that has run out of memory or simply stopped responding.
- Lowercasing a long name no longer writes off the end of a 2,048 character buffer and takes the game with it. That path handles every file inside an archive and every map name, including one arriving from a host over the network, so it was reachable from outside.
- A mission script that waits for a line of speech to finish waits the same length of time on every machine. It used to ask the sound hardware how long the file was, so a player whose sound card was not working, or was turned off, got the answer zero and walked straight through waits that everyone else sat out. That is a different game on two screens, and a replay that stops matching the machine that recorded it. The length now comes out of the file itself: measured on one line of speech, a silent machine said 0 milliseconds and a machine with sound said 3,285. Both say 3,285 now.
- Laying a foundation costs less the further up the map you build. Flattening the ground under a new building scanned every row of the map from the bottom edge up to the building, threw all of them away bar the ones actually under it, and did that once per column. It works out the rows it needs now. The ground comes out identical, down to the last unit of height.
- EVA announcements that were queued behind another one no longer go quiet. A request waits its turn and expires if its turn never comes, and the sum that worked out when that was could wrap round to zero, retiring the announcement on the frame it was asked for.
- A windowed game has a minimise button, like every other window on your desktop. There was none, and the system menu offered a Minimise entry that did nothing; the only way out was alt-tab.
- Mods can mark a thing as not worth an alarm. Walls, mines and scenery are shot at constantly and each one shouting drowns out the attack that matters; NO_ATTACK_WARNING on an object says damage to it raises no ping, no radar flash and no EVA line.
- "Your base is under attack" fires for the attack you are actually under. The check that decides whether a warning is a repeat of one you just heard was computing something that was not a distance at all, so an attack on the far side of the map could silence the warning for one at your front door, and two hits on the same building could each get their own.
- Scorch marks and tank tracks keep their colour on a brightly lit map. The shade was worked out from the map's own lighting with nothing holding it to a byte, and the red channel sits sixteen bits up, so anything over full brightness spilled into the transparency and the mark came out the wrong colour at the wrong opacity.
- Two tanks shelling the same spot no longer wipe a scorch mark off the far side of the map. With the ground already carrying its full quota of marks, a repeat crater threw the oldest one away and then declined to add itself, so a mark vanished and nothing replaced it.
- A crater is drawn whole or not at all. When the ground ran out of room for marks, the last one was drawn up to wherever the room ended and stopped there, a strip of it missing. There is room for four times as much ground under marks before that happens, and when it does, the oldest marks are the ones left out.
- Embers and sparks that trail an effect finish their run. When the effect that owned them ended first, they lost track of where it was, were drawn at the corner of the map and died early.
- Heat haze wobbles in both directions. The vertical part of the distortion was rolled, clipped against the top and bottom of the screen, and then never used - every shimmer pulled the picture along one diagonal.
- The Microwave Tank's heat haze no longer wears a black cloud. The shimmer bends a copy of the picture behind it, and on the new renderer it read that copy while the picture was still being painted into it, which the graphics card answers with nothing at all: a dark blot, fading out towards its edges. It now reads the picture as it stood the moment before the shimmer was drawn.
- Campaign briefings go grey again instead of black. The first American mission opens on Baikonur and the train depot in black and white, with the briefing lettered over the top; on the new renderer the grey step was the one draw it had no version of, so the picture went away to be greyed and never came back, and only the lettering was left on a black screen. The grey is drawn there now, and the fade into it and out of it with it.
- One tree in ten stood dead still in the breeze. The number that picks which way a tree sways was drawn from a range one short at the bottom, and the bottom of that range is the slot meaning "no sway at all".
- Police car lights flash at their own speed instead of your monitor's. On a fast screen with the frame cap off they were cycling five and a half times too quickly.
- Ten more places that wrote terrain, roads, bridges, trees, foundations and scorch marks into a buffer the graphics card had refused to hand over. Every one of them wrote to address zero instead, and a graphics device pulled out from under the game is all it takes; the frame is simply skipped now.
- Your Windows login and your computer's name stay on your computer. Both were broadcast to every machine on the network as part of the game announcement, so anyone in the lobby could read them; the tooltip they fed now shows the player name you chose, which is the part it was for.
- Leaving a network game leaves it. Quitting cleared the other players' queued orders out of one connection and then stopped, because taking a message off the queue also cut its link to the next one, so the rest stayed and kept being passed along and you sat on the game over screen.
- A malformed packet claiming to come from a player who is not in the game no longer reads and writes past the end of the game's player tables. The player number arrives over the network and was used to index them directly, in the chat, the file transfer progress, the frame relay and the disconnect screen.
- Confirming the "recorded by an older version" warning plays the replay you picked. The menu behind the warning stays live, so deleting a replay while it was up rebuilt the list, and the row number the warning had been opened on now belonged to something else - which is what you got, with nothing to tell you.
- The building outline and the target circle stay where they are when the cursor crosses the skyline. There is no ground above the horizon, and the game took the miss for an answer: the corner of the map. Every time the mouse went over the horizon the ghost you were placing shot across the world and back.
- The radar stops reading a hero who has just died. It kept the unit's own position rather than a copy of it, and only rebuilt that list every sixth frame, so the five frames after a hero fell were spent looking at a unit that was no longer there.
- A mission that asks to see more of the ground gets more of it. The script action for widening the drawn terrain was written when the game drew a square of map around the camera and pushed that square out; this one already draws the whole map, so the same arithmetic handed back a patch a few hundred feet across and the ground ended in mid air - in exactly the cutscenes that asked to see further.
- Tank tracks no longer crash the game. The renderer has to borrow the buffer it writes them into, and when that request failed, which is what a graphics device pulled out from under the game does, the tracks were written to address zero anyway. Two more of the same shape went with it: the screenshot path and the texture size lookup both used a pointer that the failing call had never filled in. Three of those texture lookups also leaked the surface they asked about, once per procedural texture, for the life of the process.
- Double-clicking a menu button opens the screen once. The button stays live while the old screen animates out, so the second click started the same transition again and the screen opened twice, one copy left drawn over whatever came next and doing nothing sensible when clicked.
- A river's banks stop coming out as a hard dark edge. The fog of war was painted onto a river in a pass of its own that redrew the whole rectangle and knew nothing about where the water fades into the shore, so the shore was darkened twice, once as ground and once as river, and the soft edge landed as a straight line with a slab of water inside it. The fog is part of the water now: the bank blends again, and a river under black shroud goes dark instead of leaking a little light through.
- Pointing a worker at an ally's half-finished building no longer offers to finish it. The cursor promised to resume construction and the order it gave did nothing, because the check behind the cursor asked whether you were allied with whoever owned the site, and you are allied with yourself.
- Double-tapping 0 jumps the camera to that group.
- The mouse wheel no longer cancels a camera move already in progress.

## The buildings nobody repairs, repair themselves

- Civilian buildings and tech buildings patch their own walls. Nobody else was ever going to: a neutral building is on no player's build list, a captured one is not on its captor's either, and no dozer in the game will touch either of them. So the first squad to be burned out of a hospital left it a wreck for the rest of the match, and an oil derrick that changed hands four times spent the game at a tenth of its health.
- Two seconds without taking a hit and the building starts mending, three percent of its full health a second, until it is whole or somebody shoots it again. It makes no difference whether it is standing empty, holding somebody's riflemen or flying somebody's flag.
- Your own buildings are not in this. A command center is a faction structure with a dozer of its own, and it repairs the way it always did.

## Peace time, if the host wants it

- A new lobby setting next to starting cash: three, five, ten or fifteen minutes of truce at the head of the match, or none, which is how the game has always played. LAN and online carry it, only the host can change it, and everyone in the room sees the choice before the game starts.
- While it runs, nobody can attack anybody. Not a rush, not a scout taking a pot shot on the way past, not a shell fired at empty ground in the hope the splash counts.
- The ground around a command centre is not somewhere to wait. Anything of yours standing in an enemy's burns, and goes on burning until it leaves or dies. The truce buys you the time to build, not a free walk up to somebody's front door.
- The clock sits in red at the top middle of the screen, where you are already looking: the word PEACE over the time left. You are also told when it starts, at a minute, thirty seconds and ten seconds left, and when it is over.
- The last ten seconds are counted out in front of you, one big number at a time under the word PEACE, each one landing large and fading as the second runs out. The word stays exactly where it was, the plate under it goes, and the time is replaced by a number you cannot miss - all of it at the top of the screen, off the ground you are about to fight over.
- Put a computer player in the room and the truce switches itself off, whatever the host had picked. A computer opponent does not honour a truce - it stands still for five minutes and then plays exactly the game it was going to play - so all peace time buys against one is a handicap for the person who is not a computer. The box greys out and reads off while a bot is in a seat, and remembers the host's choice for when the bot leaves. That is also why the skirmish lobby has no such box: a skirmish is played against computer players.

## The lobby has a settings page

- Every lobby now has two tabs over the bottom panel, Info and Lobby Settings, and the one you are looking at is lettered green (online, it is the greyed-out one). The host's settings live on the settings page; the map details in skirmish and the chat log in a network game come back the moment you tab away from it.
- The skirmish and LAN rooms are laid out again, on one grid. Each column heading sits over its own column instead of up to 18 pixels to the right of it. The seats and the settings page underneath them start and stop at the same two edges, and Play Game stands under the map and your win record on the right-hand pair. The open tab used to be lettered in a grey that measured about 2 to 1 against the panel and read as a button that was switched off. Play Game waits at the bottom right and the button that leaves the room sits at the bottom left, as far from it as the screen allows. With all eight seats taken, "USA Super Weapon General" still fits in its box. The online room keeps its old layout.
- Skirmish keeps starting cash and the game speed slider there. LAN and online keep starting cash next to peace time and the superweapon rule. Before this they were scattered around whatever space each of the three lobby screens happened to have left over.
- The game speed slider stops at 60. Its last notch used to read "--" and started the match at a thousand game frames a second, which no machine can play and every machine tried to. A speed saved on that notch comes back as 60.
- A Unit Limit box on the settings page caps the match at 840 units, shared out evenly: two players get 420 each, four get 210, eight get 105. Units standing and units waiting in a queue both count against your share, buildings do not, and the build buttons grey out when you reach it. A transport that rolls out full is charged for its riders while it is still in the queue, so a Troop Crawler costs nine and cannot slip eight Red Guards past the limit, and a pair of Red Guards costs two. A shift or control batch near the wall queues as many as still fit and stops there, and a button whose next order would not fit greys out. Your own count and your share sit in the top right corner next to the frame rate, so you can see the wall coming. The host ticks it; everybody else sees it ticked.
- Superweapons are a dropdown instead of a tick box. Allow Superweapons is the game as it plays without a rule. Limit Superweapons leaves each player one Particle Cannon, one Nuclear Missile or one Scud Storm at a time. No Superweapons bars them.
- No Superweapons can be picked. The dropdown sits low on the settings page, and its last row hung past the bottom edge of the panel: it drew, but a click on it went to nobody and the list stayed on whatever it said before. Any dropdown row past the edge of its panel takes the click now.
- The USA Superweapon General is the exception to both, because the rule that reads as a handicap to everyone else reads as a deletion to him: three superweapons of his own are what he pays for weaker tanks, weaker infantry and a worse air force with. Under Limit he keeps four of each of his. Under No he keeps one. Play him against a lobby that has banned superweapons and you are playing the army you paid for, not an empty base.
- The tick box was one number for everybody, which is why it could only ever say yes or no. A ticked box in a settings file from an older build reads back as No Superweapons.

## Pro Rules, in every network game

- A fixed list of units, upgrades and tricks is out of every LAN and online match. There is no warning and no penalty after the fact: the button is grey and it stays grey. A skirmish against the computer has the whole arsenal, and so do the campaign and the Generals Challenge.
- It is one box on the network lobby's settings page, Pro Rules, ticked unless the host clears it. The skirmish lobby has no such box. The host's choice goes to everyone in the room and into the replay, and the lobby remembers it for next time. A host on an older build has no box to send, so that game plays without the list.
- No Particle Cannon and no SCUD Storm. The USA Superweapon General keeps one Particle Cannon, because a general who paid for his superweapons with a weaker army and then may not build any is playing half a faction.
- The Nuclear Missile silo still goes up, since it is where China buys Nuclear Tanks and Uranium Shells. The missile inside it never launches, and nobody's screen counts down to it.
- No Aurora Bomber, the Air Force General's Alpha Aurora included, and no Tactical Nuke upgrade for the Nuke General's MiGs.
- No demo bike. A terrorist cannot climb onto a Combat Cycle any more; a rebel, a worker, a hijacker or Jarmen Kell still can.
- A Microwave Tank cannot freeze a building that is still going up. There is nothing for it to fire at until the scaffold is finished, so parking one beside a half-built base no longer keeps it half built.
- No foundation goes down closer to an enemy building than 300, a margin past the 225 a Patriot battery or a Stinger Site reaches on the ground. That ends walling somebody's base in with scaffolds nobody means to finish, and it ends building a tower in their yard too. The placement ghost says why it is red.
- The computer opponent is held to the same list, because it asks the same question your build buttons ask before it spends a dollar.
- With these rules on, the superweapon dropdown above decides one thing: how many Nuclear Missile silos a player may have standing.
- No suicide bomb upgrade for the Demolition General, no Neutron Shells and no Stealth Comanche. The Carpet Bomb stays grey until its owner reaches three stars, which holds back the Air Force General, who can buy his at one.
- Two towers per oil derrick. Your Patriot batteries, Gattling Cannons and Stinger Sites near a derrick count against it, and derricks standing together on the map share one allowance, so the pair 250 apart on Tournament Plains is one derrick for this. The third tower's placement ghost goes red and says why. Your ally's towers do not use up your two, and tunnels are not limited.

## Two windows, one machine, a real LAN game

- You can start the game twice on one computer and play the two copies against each other over the LAN screen, the ordinary way: one hosts, picks a map and starts, the other finds the game in the list and joins. Faction, colour and team per slot as always. It is how a network change gets tested here, and it is how two people sharing a desk can play a match without a second machine.
- The second copy used to come up with a socket error and an empty game list, because the lobby's one network port was already taken by the first. Each copy now takes an address of its own on the machine's own loopback range and they announce themselves to each other across it.
- The second copy also used to be thrown out on the way in. It arrived carrying the same player name, which a host refuses, and the same serial number, which a host refuses harder - a rule written against two people sharing one bought copy, and two windows on one desk are not that. Both copies get their own name now, and the serial rule is waived only between two addresses on the same machine. Anything that could be a second computer still answers to it.
- `lan-play.ps1` starts as many copies as you ask for, gives each its own name, address and log, opens each on the LAN screen instead of the main menu, and lays the windows out side by side so they are not stacked on top of one another.

## Whose side is that

- Player colours can say who is on your side. The original game hands out eight colours picked for being different from each other, and none of them means anything: in a four-player fight, working out whether the tanks coming over the ridge are your ally's or the other team's is a memory exercise done at speed, and getting it wrong costs you the fight.
- You, allies, enemies paints you blue, everyone fighting alongside you green and everyone shooting at you red. Members of the same side get different shades of the one colour, so four allied armies are still four armies and not one green blob.
- By team gives each alliance its own colour and each of its members a shade of it. That is the one to use as an observer, watching a 4v4 where nobody is yours.
- The colour reaches everything: the tint on the models, the radar, health bars, selection rings, the score screen, the money that floats up from a supply drop.
- Your screen only. Nobody else in the match sees your choice, nothing about the game itself changes, and two people in the same game can run different settings. Original is the default and nothing changes until you pick something else.
- Switch it mid-match and the map repaints itself. So does the picture when an alliance is made or broken by a map script.
- The player names on an observer's command bar follow the setting as well. They used to keep whatever colour they were given when the bar first filled, which was sometimes black.
## You can see where your ally is pointing

- Your ally's mouse is on your map: a soft pool of their own player colour on the ground where it rests, with their name across the middle of it. No second arrow on your screen - one is confusing enough, and the name is the part that says whose it is.
- Two people playing together spend half the match describing places to each other. "Behind the oil derrick, no, the other one" is a sentence nobody has to type when you can watch where they are pointing while they say it.
- It moves the way a hand moves. Ten positions a second go over the wire and the marker glides between them, so what you see is somebody working rather than a marker teleporting around the map.
- Allies only, and only mutual ones. Your position is sent to the players you are actually allied with and to nobody else; an enemy is never sent it at all, and a machine that has been made to send it anyway is ignored by everyone who is not your ally.
- An ally who alt-tabs away fades out over a couple of seconds instead of freezing on the map, so a marker that is still there is a marker somebody is still behind.
- Off in one place. `ShowAllyCursors = no` in `Options.ini` stops both halves: nothing is sent from your machine and nothing is drawn on it.
- Alt+Z, Alt+X and Alt+C put up a puff of smoke in your own colour where your mouse is, with ATTACK, DEFEND or LOOK written over it. The colour says who, the word says what. Point at the radar instead and the smoke goes up on that spot of the map.
- Your allies see the smoke and their radar blinks at the spot, and the message area says who sent it. Three seconds later the smoke has cleared, so the call you made a minute ago is never still standing next to the one you are making now. Enemies see none of it.
- One signal a second per player. Hammering the key gets you the first puff and nothing else, and a teammate who tries it cannot bury your screen in smoke either, because every machine in the game holds everyone to the same limit.

## The options screen has pages

- Display, graphics, effects, audio, controls, gameplay and network, behind seven buttons across the top. The original screen was one panel with everything on it at once, and it was already full the day it shipped: the language filter, the keyboard button and four camera checkboxes are all still in there, parked off the right edge where nobody can reach them, because there was nowhere left to put them.
- Every graphics setting is on the Graphics and Effects pages. Shadows, clouds, ground lighting, soft water edges, trees, extra animations, heat haze, texture resolution and the particle limit used to hide in a popup that only opened if you picked Custom from the detail box on another page, so choosing High never showed you what High turns on. Pick a preset now and its settings change in front of you; touch any one of them and the box reads Custom. Texture filtering and the number of anisotropic samples, which needed a text editor, sit there too.
- Every page is sorted into titled groups by what the settings do, and the groups sit in the same three columns on every tab. On Graphics the detail preset heads the first column with the texture and particle sliders under it, the picture settings take the second and the terrain the third. The framed boxes the original drew inside each page are gone. They were sized for headings that no longer exist, which is why no two pages started their first setting at the same height.
- The detail box has Ultra above High, and a preset now sets everything it is responsible for. Picking High used to leave antialiasing and texture filtering wherever they happened to be; High now brings 4x antialiasing and 8 anisotropic samples with it, and Low turns both down for the machines that need it. Ultra is High with the particle limit at the top of its slider, 5,000 against High's 3,000, detail that is never dropped when the frame rate dips, 8x antialiasing, 16 anisotropic samples, thick smoke and every new shadow and effect switched on. Glow stays yours whichever you pick: the artwork was painted without it.
- Effects is a page of its own, with seven settings that used to be command line switches or were fixed in the game's data: shadows for infantry, for missiles and bombs, for scenery and for smoke clouds, thicker smoke in two strengths, particles that land on the ground instead of falling through it, and swaying trees split away from extra animations, so the scaffolding can stay while the forest holds still. The shadows take effect on the next map, smoke and particles the next time the game starts.
- Every slider shows its value beside it: brightness, the three volumes, scroll speed, texture resolution, particles and anisotropic samples. Every label now starts on the same edge as the box under it; the original drew each one a few pixels to the right.
- The tab you are on is lit in the same green as the group headings. The original drew it in dark grey on the black panel, which read as a button you could not press rather than the page you were looking at.
- Accept sits in the bottom right corner with Cancel beside it, and Defaults stands alone on the far left, out of reach of a slip off Accept. The window is as tall as its busiest page and centred on the screen, instead of reaching from the top of the screen to the bottom with half of every page empty.
- Eight settings that used to need a text editor have a control now: window mode on display, antialiasing, the two glow settings, texture filtering and anisotropic samples on graphics, and who wears a health bar and which colours the players wear on gameplay.
- Twelve more are decided for you instead of asked about. Grid placement, nudging a blocked building, 45 degree building rotation, the placement range ring, workers going back to supply, detailed build tooltips, the HUD overlay and archived replays are simply on - every one of them is the version of the game people were choosing anyway, and a page of eight checkboxes nobody unticks is a page of eight decisions nobody wanted to make. Middle-mouse panning left the screen too, and edge scrolling in a window and 45 degree camera steps are still yours by name in `Options.ini` if the defaults are wrong for your setup.
- A page no longer says its own name twice. Every tab used to open onto a heading repeating the word already written on the button you just pressed, with a rule under it dividing nothing from nothing.
- An open dropdown covers what is under it instead of the other way round. The list of resolutions grew downwards behind the labels below it, so half the entries were readable and half were words on top of words.
- Every button, slider and checkbox is the artwork the game already had, down to the fonts and the hover colours.- Writing a line into `Options.ini` by hand still works, and the screen shows you what is in there when it opens. Window mode takes effect the moment you accept it; antialiasing applies the next time the display device resets.
- The Defaults button still resets what it always reset, which is the original settings. The new rows keep their values.

## The whole game in Turkish

- Options, Gameplay, Language: English or Türkçe. Pick Türkçe and the next launch has Turkish menus, command bar buttons and their tooltips, unit and building names, general's powers, mission briefings, objectives, hints, loading screens, score screens, the lobby, the strategy advice after a match and the credits. That is 3,853 lines, every one the game shows except the ones that read the same in both languages: multiplayer map names, people's names, and unit codenames like Crusader and Overlord, which Turkish players call them anyway.
- It reads like a game, not a dictionary. One glossary of more than 1,400 terms fixed every name before a line was written, so the building your tooltip calls Savaş Fabrikası is a Savaş Fabrikası on the button, in the briefing and in the hint. GLA briefings keep their zeal, Chinese ones their drill and American ones their clipped radio voice.
- Percentages are written the Turkish way, "yüzde 25", and every number the game fills into a sentence lands where it should. A check runs over the whole file on every build and refuses a line whose blanks do not match the English, the mistake that turns a translated tooltip into a crash.
- Voices and videos stay as your install has them. Your language is your own screen's business: two players in one match can read it in two languages.
- A line the translation does not carry falls back to English instead of showing a key name, so a string added later never leaves a hole.

## It fits your monitor

- The writing on screen grows with your monitor now. Every panel is stretched to your resolution and always was, but the text inside it stayed the size it was drawn at in 2003, and what growth there was stopped dead at twice - so on a 2560 wide screen a command bar three times its original size still wore eight point lettering. The keys on the build buttons, the prices and the build times were the worst of it: at 2K they were drawn, and unreadable.
- The strip of everything your base is building follows the screen too, instead of staying a row of postage stamps under a command bar three times its size.
- On a widescreen monitor the command bar is three pieces instead of one stretched strip. It was drawn for a 4:3 screen and every measurement in it is multiplied by your resolution separately across and down, so at 16:9 the whole thing came out a third wider than it is tall: square build pictures as rectangles, and one slab of metal across the entire bottom edge. The radar now sits hard in the left corner, the build grid in the middle, the selected unit's panel hard in the right corner, each at the size it was drawn at, and the battlefield shows through the two gaps that opens. At 4:3 the three pieces meet and it is the bar it has always been. The three pieces reach the screen: the paintings stop a pixel or two inside their own edges, which left a hairline of ground down both sides and along the bottom, and anything that close to an edge is now that edge. The American middle piece stands on the bottom edge like the other two sides' do, instead of hanging ten units high with a band of battlefield under the build grid, your money now sits in the middle of its readout instead of riding the top rim of it, and the build buttons are centred in the panel they are drawn on instead of pushing against its left edge with a strip of empty metal on the right. The stray pixels along the right-hand edges of the pieces are gone: a one-pixel light line down the side of the middle piece, and a lit speck sitting in the battlefield next to the bottom corner of the Chinese radar. Both were the same thing. The bar's artwork was being rescaled as it went to the graphics card, which left the last column of each picture holding a mirrored copy of it, and the last column of pixels on screen drew from there.
- The rest of the interface is drawn at the command bar's size, not the screen's width. The queue strip, the superweapon countdowns, the generals' powers bar down the right edge and the rank screen were each measured against how wide your monitor is, and the bar they sit beside is measured against the smaller of the two dimensions - so on a 16:9 screen a queue picture came out a third bigger than the build button it is a picture of, and the power slots were oblong where they were drawn square. One size for all of it now, and it is the bar's own.
- The generals' powers bar stands on the right edge of the screen instead of hanging off it. It slides in from the right when a power is ready, and the slide stopped one step short of where it was going and left the window there - twenty-one pixels out at 1024x768 - so the rightmost column of powers was drawn half off the monitor and could not be clicked. Every panel in the game that slides in sideways had the same fault; the ones that slide up and down never did.
- The writing on a build button keeps its size against the button. The hotkey letter, the price, the build time and the queue count were sized off how wide your monitor is while the bar they sit on is sized off the smaller of its width and its height, so the wider the screen the further the lettering grew away from the cameo underneath it. One size for both now, and it is the bar's.
- Text keeps pace with the panel it sits in. The original grew its lettering at seven tenths of the rate it grew the panels, so the bigger your screen the more empty metal surrounded every label: at 1920x1080 a tooltip's text was two sizes smaller than the box drawn for it. It grows at the full rate now, which puts it at exactly the proportions the artists laid out at 800x600.
- Text is heavier. Every letter's soft edge was two thirds see-through, so light writing on the dark panels thinned to hairlines at the sizes tooltips and buttons use. The edges keep more of their weight now, while the solid middle of each stroke is unchanged.
- Build tooltips no longer paint one letter of the unit's name yellow. That letter was the old one-key shortcut, and the grid keys on the buttons replaced it; the yellow copy was drawn on top of a gap left in the name, so any letter the yellow pass could not draw, the ş in a Turkish name for one, simply went missing from the word.
- An ultrawide monitor gets the same interface as a 16:9 one. Everything still sized off how wide the screen is came apart at 32:9: on a 5120x1440 screen the lettering was drawn at two and a half times what the panel under it had grown to, the clock and speed readout crept into the top corner because its margins were flat pixels, and a health bar reached across the whole base of the building it belonged to. All of it now measures off the same number the command bar does. Scale a 32:9 screenshot down to 16:9 and the two lie on top of each other.
- The camera shows the same ground whatever shape your monitor is. The game holds the angle it sees across the screen fixed and lets the up-and-down angle shrink as the screen widens, which at 32:9 left fifteen degrees of world against twenty-nine at 16:9 - the same barracks filled twice the height of the screen, and it read as a zoom nobody asked for. Above 16:9 the view opens sideways instead, so a wider monitor buys more battlefield rather than a closer look at less of it. Nothing at 16:9 or below moves.
- The camera reaches the corners of the map. It stopped short of every edge by an amount that changed with the zoom and the angle, so a base in a corner never came to the middle of the screen. It now goes 200 units past any edge of the map, whatever the zoom. `CameraBoundaryMargin` in `Options.ini` sets how far, and 0 puts the old limit back.
- The medals on the player info card are round on any screen. They were stretched across and down separately, so a wide monitor turned each one into an oval.
- Minimising the command bar sends all three panels down, and they slide back up when you bring them back. The old minimise dropped the bar a tenth of the screen and left a strip of metal, a readable money box and the top of the radar dish lying along the bottom edge, which is neither the bar nor out of the way. The radar and the middle go off the screen entirely; the selection panel stops with the row its minimise tab is in standing on the bottom corner, so what you click to bring the bar back is that tab with its own artwork behind it, not a button floating over the battlefield. 
- Everything on the command bar that each faction places for itself lands where that faction's artwork paints it. The bar's own arithmetic was overwriting those positions with the ones it had used a moment earlier, so every panel wore the generic layout's vertical placement instead of its own: the minimise button drew a second tab below the painted one, arrow and all, and the GLA money sat high enough in its box to touch the frame. One arrow now, one tab, and the number in the middle of its readout - and the same correction reaches the worker, beacon, chat and options buttons down the side of the grid. The American power bar sits in its rail as well: it was drawing on the ledge above the rail, which put the left half of the bar on the battlefield rather than on the bar, and left the rail it reads in standing empty.
- Two stray letters, an "S" and an "M", no longer appear over the battlefield beside the selection panel the first time you minimise the bar and bring it back. They are the labels on the two unused minimise buttons the 2003 layout ships hidden; minimising hid them a second time and un-hid them on the way back.
- The markings on a build button - the shortcut letters, the price, the build time, the queue count - are set bold. At seven points against a busy cameo the light weight read as part of the picture.
- Menu panels slide in at the same speed whatever your monitor is. They travel the width of the
  screen but moved at a fixed forty pixels a step, so the wider the screen the longer every
  transition took: twenty steps at 800 wide, sixty-four at 2560.
- A screenshot no longer takes the game down when the window is bigger than the desktop. Asking for
  a 1600x1200 window on a 1080-tall monitor puts part of it off the screen, and the capture read
  the whole client area out of a buffer that stops at the desktop's edge.
- A screenshot is a picture of the game. With anti-aliasing switched on, the capture could not read
  the frame the game had just drawn, so it photographed the desktop instead and said nothing about
  it. In a window with something in front of the game, that something is what you got: a browser, a
  chat window, whatever was on top. It reads the frame itself now, anti-aliasing or not, and what is
  in front of the window makes no difference to what comes out.
- The arrow on a dropdown is square at every resolution. It was drawn into a slot of a fixed 21 pixels wide however tall the box around it was stretched, so the bigger the screen the more the arrow was squeezed.
- The promotion screen closes when you press its key again, however fast you press it. The screen fades in, and the fade drives the window itself for several frames either way - so a second press during the fade read the screen as still shut and opened it again, and a press just after it was closed was undone by the fade's next frame.
- The command bar stops changing under you while you are using it. With nothing selected the bar shows one of your builders so you can place a structure without picking a dozer first, and it showed whichever one happened to be idle - so a dozer finishing a building on the far side of the base took the bar over, dropping a half-typed build hotkey and taking the structure off your cursor. The builder you are working with keeps the bar.
- The command bar's artwork moves with the command bar. Minimising it slid the buttons off the bottom of the screen and left the metal they sit on painted over the battlefield; the same painting stood still through the slide the bar makes when a match opens. It travels with the bar and goes with it.
- Play it the way it shipped. Options > Controls has a Mouse and Keyboard box with two answers. Modern is this game as it is now. Legacy is the 2003 game: left click selects and orders, right click deselects, a right drag scrolls the map, a middle drag turns the camera, and the command bar answers the game's own letter for each button, painted in the button's corner the way the grid keys are. Holding Ctrl makes a click force fire, at the ground or at your own units. Those letters are the English game's even when the words are Turkish: the translation marks every label's first letter, which would have put K on three GLA structures and S on five. The promotion screen's number keys and the general's power keys are this game's additions, so under Legacy their labels are gone and the number keys pick your groups again. Every key comes from the game's own key map, so the grid keys, the two-key structures, Alt+Z signals, Ctrl+Z barracks, Tab, box select filters, formation lines, zoom to cursor and clickable health bars all stand down. It changes the moment you press Accept, even in the middle of a match, and it only changes your own screen.
- The lines from your selection to where it is headed can be switched off. Options > Gameplay has an Order Lines box, on by default. Off, a selected unit shows no line to its goal and none through its queue, but the marker on every point it is headed for stays, so an attack move or a string of waypoints is still there to see. The line you draw while dragging a formation still shows, because that one is the order being given. It changes the moment you press Accept.
- Zoom to cursor is a box on Options > Controls again, on by default, for anyone who would rather the wheel zoomed on the middle of the screen. It also stopped fighting you. The camera corrected itself a frame after each step of the zoom, so the ground slid off the cursor and was pulled back the whole way in, and the last pull landed after the zoom had finished, as a small jump towards the cursor. And a wheel spun over a spot with no ground under it, out past the edge of the map, threw the camera to the corner of the map. The ground under the cursor stays under it on every frame now, and a spot with no ground zooms on the middle of the screen.
- Click through the command bar wherever its metal is not. The bar is three shaped plates with sloped tops and cut corners, but it caught clicks over the whole rectangle each plate sits in, so a unit standing in the sky above the radar's slope, or in the notch beside the command grid, could not be clicked or ordered to. Between 3 and 22 percent of each plate's rectangle is empty, depending on the side and the panel. Those parts belong to the battlefield now, and the buttons and metal still take every click they did.
- Drag the camera across the radar. Press on the radar and keep the button down, and the camera follows the cursor over the map until you let go; before, it jumped once per click, so crossing the map meant clicking your way over it. The button that moves the camera with a click is the one that drags it, left under Modern and right under Legacy, and with nothing selected either one does. Run the left button off the side of the radar and the camera waits at the edge of the map. It keeps following while the arrow keys are scrolling too, which used to leave the drag dead until the key came up. A selection box dragged onto the radar from the battlefield leaves the camera where it is.
- Widescreen resolutions are back in the options menu, and the list works during a match. It was greyed out the moment a game started, because changing it rebuilt the menus and then pushed the main menu over whatever you were playing. It rebuilds the menus for later and leaves you on the battlefield with the command bar back up.
- Changing the resolution during a match no longer takes the game down. Two things were behind it. The old command bar's windows were handed back while the menu system still had them on its list, so the next thing that painted a button read whatever had since been written over it. And the bar itself was thrown away and built from scratch, which handed out fresh copies of every build button while your factories, your repair crews and the promotion screen went on pointing at the old ones. The bar's windows are rebuilt now and the bar keeps everything that is not a window.
- One command bar, not three. Changing the resolution during a match rebuilt the bar and left the old one behind, still drawn and still taking clicks at the size of the screen it was built for - and the confirmation box you answer afterwards rebuilds it a second time, so two mode changes in a row put three command bars on top of each other across the bottom of the battlefield. The code that was meant to throw the old one away asked the window system for a window called "ControlBar.wnd", and the bar's window is called "ControlBar.wnd:ControlBarParent", so it never found anything to throw away and never had.
- Your money is still your money after a resolution change. The command bar is rebuilt at the new size and its readout comes back holding the placeholder its layout ships with, three dollar signs, and the game only writes a number into it when the amount changes - so until you next sold, spent or collected something, that is what the box said.
- The menus you already had open are rebuilt at the new size too. A panel is stretched to the screen once, when it is built, so everything on screen when the mode changed - the in-game menu, the diplomacy panel, the chat line, the replay controls - carried on wearing the shape of the screen that had gone: drawn a proportion of a screen away from where it belongs, and on a smaller screen partly off the edge. The in-game menu is the one that mattered. It pauses the match and takes input off you until you press Return, and pressing Return is impossible when the button is no longer on the monitor.
- The command bar stays on the bottom edge of the screen. Minimising it and bringing it back lifted a few pieces that had never gone down in the first place, and every rebuild of the layout lifted them a little further, so over a long match the bar climbed off its own strip and the build tooltip, which is placed against it, ended up above the top of the screen. Each piece is now put back by exactly the amount it was moved by.
- The other half of that had the money readout, the general's tabs and the column of small buttons stranded a couple of hundred pixels above the bar, floating over the battlefield with the bar itself still sitting where it belongs. The bar takes a fifth of a second to slide back up when you un-minimise it, and anything that redrew the panel artwork during that fifth of a second measured every one of those pieces against a bar that had not arrived yet, then wrote the wrong answer down as if it were the right one. Switching general, changing faction colours and a resolution change all redraw it, so in a long match it happened sooner or later. The bar is put where it belongs before any of that is measured. Over six thousand frames of a match that minimises and rebuilds the bar every second, the top edge of everything it owns now reads the same number every single time.
- Tooltips stay on the screen. A tooltip is drawn to the right of the pointer, and near the right or bottom edge it jumps to the other side to make room, which was all the original code did about edges - the man who wrote it left a note saying so. Nothing then checked where it had jumped to. A wide one, and the game will wrap a tooltip to the full width of the screen when the text asks for it, jumped clean off the left edge with the first half of the sentence outside the monitor; a long narrow one hovered near the top went off the top. Both are put back on the screen now, and the pointer sits over the corner of the box rather than the words going where nobody can read them.
- The rank screen is one painting instead of a stretched one. It is sized from that painting at the same scale as everything else and centred on the screen, rather than multiplied out separately across and down from the corner it was authored in - which on a widescreen monitor made it a third too wide and pushed it off centre.
- Borderless fullscreen: start with `-borderless` and the game fills the screen at your desktop resolution with no frame and no display mode change, so alt-tab is instant, nothing on the desktop gets shuffled around behind it, and a second monitor stays usable. Screen-edge scrolling works there, the way it does in fullscreen. The loading picture still opens as its own small window with your desktop around it, and the game only takes the screen once it has something to draw.
- Borderless and fullscreen keep the mouse on the game's own screen while you are playing. With a second monitor beside it the pointer slid straight over the edge onto the other screen, so edge-scrolling toward that side did nothing and the next click landed on your desktop. The game has asked for the pointer to be held since 2003, but only a mouse driver the game never uses ever did it. Alt-tab still lets go, and coming back takes the pointer only once you move it over the game again. A plain window leaves it free, as before.
- Fullscreen, borderless and windowed are one saved setting now, so borderless no longer means adding a switch to your shortcut every time. `WindowMode` in `Options.ini` takes 0, 1 or 2 in that order; a switch on the command line still wins for that one run. Picking a different one applies when you press Accept instead of the next time you start the game: the window loses or gains its frame and the display device is rebuilt on the spot. Borderless takes your desktop resolution and nothing else, so while it is selected the resolution list greys out and the other two modes hand it straight back.
- Antialiasing is a saved setting as well: `MSAA` in `Options.ini`, 0 for off through 4 for sixteen samples. `-msaa 4` still works and still wins for that run. Whatever your card will not give is stepped down instead of refused, so asking for six samples gets you four rather than nothing.
- Texture filtering is yours to set. `TextureFilter` in `Options.ini` takes 0 for bilinear, 1 for
  trilinear and 2 for anisotropic, and `Anisotropy` takes the sample count, 0 meaning whatever your
  card offers. The default is anisotropic at the card's maximum: retail shipped bilinear with point
  mip selection, which was a 2003 fill-rate budget and is why distant ground shimmered.
- Picking High detail gives you high resolution textures. The setting was ignored: the resolution came from a machine benchmark that answers 'low' on anything modern, so there was no way to ask for better.
- The main menu plays its battle again instead of showing one still picture. The game asks Windows how much memory the machine has and compares the answer against a 256MB minimum, and on a machine with more than 4GB installed that answer arrived as minus one - so the better the machine, the more certain the game was that it did not meet the 2003 minimum. It switched off the animated menu, forced textures down and, at the preset detail levels, took the trees out. It reads the real figure now.
- A setting you typed into `Options.ini` by hand survives opening the options screen. A value the
  matching slider could not reach was thrown away rather than clamped, and the slider then sat on
  its minimum - which is what got written back when you pressed Accept. Whatever the slider cannot
  show now reads as the nearest end of its track instead of the bottom of it.
- The Default button in the options menu no longer throws away your resolution. It reset the display along with everything else, dropping you to 800x600 with no undo.
- Text does not vanish at large sizes. Any font over 100 points simply failed to load, which on a 4K screen is a missing line of interface.
- Zoom further out, with the whole map drawn instead of black corners. The ceiling is twice what it was again, which is the difference between watching your own half of the map and watching both.
- The zoom-out ceiling is the same for everyone; no setting buys you a wider view.
- The wheel covers that whole range in about six notches instead of thirty-eight.
- Zoom toward the cursor works; the spot under your pointer stays there.
- The camera stays above the ground. Zoomed in on a slope it used to end up inside the hill it was looking over, and the world opened up along the near plane.
- The far edge of the view opens with the height, instead of stopping at a distance fixed for the stock zoom - which is what put black beyond the terrain when you zoom out past it.
- The box on the radar follows the camera when you pan. It only ever redrew itself when the zoom or the angle changed, so scrolling left it behind.
- The camera turns in whole 45-degree steps, instantly, while you hold the key.
- Edge scrolling works windowed, and scroll speed no longer follows your frame rate. It also stops when you take the pointer off the window. Windows says nothing about a cursor that has walked out onto the desktop, so the game kept reading the last edge it crossed and the map slid on its own until you brought the mouse back. A game that has only just opened asks Windows where the pointer actually is instead of assuming the top left corner, which is an edge like any other and used to drag the camera there before you had touched anything.
- Taking over another base no longer squeezes the picture into the top four fifths of the screen.
- A skirmish opens zoomed all the way out (`StartAtMaxZoom = No` restores the old opening).
- Hold Ctrl and roll the wheel to turn a building before placing it.
- Buildings snap to the pathfinder's own grid (`GridBuildPlacement = No` for the old way).
- The grid under a structure is drawn, with blocked squares crossed out.
- Snapping now uses the true cell offset and the building's concrete apron.
- A building faces the way you drag it, read at the moment you release.
- The mouse pointer stays visible for the whole placement.
- How see-through the structure on your cursor is, and whether it drops a shadow, are yours to set:
  `BuildPlacementOpacity` and `BuildPlacementShadows` in `GameData.ini`. The defaults are what the
  game always did.
- The menus move at whatever speed you want them to. `MenuTransitionSpeed` in `Options.ini` is a
  percentage: 100 is the pace the slides and fades were drawn at, 400 gets you from the main menu to
  a skirmish setup in a quarter of the time, 25 is for looking at them.
- The right mouse button can stop dragging the camera. With the alternate mouse layout it is the
  selection button, so every selection drag was also a camera drag; `RightMouseScroll = no` in
  `Options.ini` hands it back and leaves the middle button and the screen edge to scroll with.
- Alt+F4 and the window's close button quit the game. Both have always sent the message that means
  "leave now", and nothing was listening for it: the handler was switched off in 2003 and never
  switched back on, so the X in the corner did nothing and the only way out was the menus. A match
  in progress stops recording its replay first.

## Soldiers cast real shadows

- Infantry shadows are built from the pose: arms, head, weapon, moving with him.
- `UseShadowVolumesForSkins = No` puts the old flat blobs back.
- Scuds, rockets and falling bombs cast a shadow running along the ground.
- An aircraft's shadow lands where the sun puts it, not underneath the aircraft. Every plane and helicopter in the game's own data asks for a sun "no lower than 89 degrees", so each one cast its shadow straight down and towed it along directly below itself at any height, while the tank beside it was lit by the map's real sun and threw its shadow off to the side. Aircraft now take the same sun as everything else: a helicopter's shadow slides out from under it as it climbs, and a plane's runs ahead of or behind it depending on the hour the map is set at. On a map lit by a very low sun the shadow is held to what a 30 degree sun would cast, so it does not stretch across half the screen. The flat blob shadows under missiles and bombs follow the same ray.
- A big smoke cloud darkens the ground under it and fades as it does.
- The soft blob shadow for trees and scenery had never been drawn at all.
- Every one of the 128 tree types casts now, plus the bushes and palms.
- A tree's shadow is the tree. Trunk, crown, and the gaps between the leaves, lying along the
  ground in the direction the map's own sun points, and stretching the way a low evening sun
  stretches one. It sways when the tree sways, leans away from the tank pushing past it, and falls
  over with the tree when the tree comes down.
- The palms too, and every other tree the map placed as a real object rather than scenery. There
  are 278 of them on Golden Oasis and they had nothing under them at all; their shadow runs up and
  over the dunes instead of lying flat, because it asks the ground how high it is at every point it
  covers.
- Fences, walls and props cast a real shaped shadow, worked out once.
- Each of these switches off in `GameData.ini`.

## Bright things can glow

- Water reflects what stands over it. The pools on Golden Oasis hold their palms upside down and the
  stone bridge's arches under the bridge, and the lakes on Fortress Avalanche carry the pines on
  their banks. Helicopters hovering by the river show dark and upside down in the water under them;
  they were left out at first, because the mirror skipped every unit the game had set aside that
  frame for its behind-building markers. The game has had a mirror in it since 2003, wired to a test sea no shipped map turns
  on; the water you actually play beside gets it now. The reflection only ever darkens: open water
  keeps the exact colour the map painted it, and a black shroud stays black. The water shows only
  what the land does: an enemy hidden under the fog of war, or a stealthed unit, used to give itself
  away in the reflection. A palm shows the same in sunlight as under the fog: whatever stands over
  the water darkens it by one amount, where a sunlit palm used to be nearly as bright as open water
  and vanished the moment the fog lifted off it. It is not free, since the world is drawn a second
  time for the mirror, but the mirror leaves the ground out, draws only the part of the screen the
  water covers, and only every other frame while the camera holds still. On one 1280x720 view of
  Golden Oasis, where the river fills the screen, a frame takes 6.7ms against 5.8ms with no
  reflection; before that work it took 8.3ms.
- Bright things can bleed light into the air. Off by default, because the artwork was painted in 2003 for a screen that had no glow at all.
- Two dropdowns on the display page, not two percentages. Glow is off, subtle, normal or strong; what glows is only the brightest, bright things, or most of the picture. The numbers underneath were a strength you had to find by experiment and a brightness that ran backwards, where turning it down put more of the screen in the haze, and nothing on screen told you that.
- Scorch marks follow the texture quality setting like everything else. The terrain and the trees
  dropped to the resolution you asked for and the burn marks did not, so on Low the ground went soft
  underneath craters that stayed sharp.
- The craters a map ships with stay on the ground. Scorch marks live in a list of 500 and the list
  makes room by throwing away its oldest entry - which is always one the level designer placed, so a
  long battle rubbed out the map's own burn marks one at a time. Only marks the fighting made are
  thrown away now.
- Units stop going see-through for no reason. The game marks a unit that is standing behind a
  building so you can still make it out, and the flag that says "yes, something is in front of this
  one" was set once and never cleared - so the first unit genuinely behind a wall made every unit
  checked after it see-through too, wall or no wall. On a busy screen that was most of them.
- Bloom and antialiasing work together. Turning bloom on with antialiasing on gave you a black battlefield under a live interface: the glow pass draws the world into its own picture first, and that picture came with a depth buffer nobody had cleared, so every triangle in the scene was rejected as being behind something that was not there.
- Lakes stop showing jagged bands of shore while you scroll. The glow and smoothing effects draw the world into a picture of their own, and that picture was never wiped between frames, so the water kept reading last frame's shoreline from wherever the camera had just been.
- A switch you write in `Options.ini` by hand takes `yes`, `true`, `on` or `1` for on. Only the
  exact word `yes` used to count, so a line spelled any other way read as off and looked like a
  setting that did not work.
- Explosions light what is around them: 89 of them, from a tank shell to the Scud Storm,
  throw a warm flash that fades over a third of a second. A night fight used to be muzzle
  smoke over unlit ground.
- Heat haze bends the picture as far up and down as it does side to side. The vertical pull was
  half the horizontal one, which nobody could see while the haze was reading its sideways figure
  for both directions, and became a visible lean the moment that was fixed.
- Cutscenes and briefings play at the colour depth your card actually offers. Every video but the
  EA logo was dropped to sixteen bit on the way in, on any machine - the test that was meant to
  limit that to a 2003 low-memory PC is commented out in the same line - and it was dropped there
  even on a device that had just reported it could not do sixteen bit at all.
- Snow falls as snow again. On the new renderer every flake near the camera was drawn as big as a
  power plant, because each winter map sized its flakes in screen pixels, capped at 10 or 16, and the
  new renderer read a different figure from the same file that has no cap at all. Flakes on Bitter
  Winter are the same size as they are on the old renderer now.

## It does not crash

- Starting a match with the Direct3D 11 picture on took the game down. Both devices were presenting into the same window, the old one lost itself, and the fog texture then locked a surface that was never made.
- A machine whose graphics stack faults while the Direct3D 11 picture is being set up starts the game anyway, on the Direct3D 9 picture. A player's report showed the fault happening inside Windows' own graphics library, before the game got an answer back, and it ended in "Uncaught Exception during initialization". An overlay hooking the graphics calls or a stray dxgi.dll in the game folder is the usual cause. Until now the only way past it was typing -d3d9.
- The opening movies froze on their first frame with that picture on. A movie writes a new image every frame into a texture the copy had already taken.
- Opening Single Player could take the game down a quarter of a second in, as the buttons flashed in. The menu's animation list names a Custom Mission button that only the 1.04 patch's main menu has, and on an install whose menu lacks it, every step of the flash skipped the missing button except one, which tried to draw it. That step skips it now too.
- Two blocks of 2003 assembly destroyed registers and took down the main menu.
- A model file with no skeleton in it is refused as it loads. It used to load, and the game went down the first time anything asked it for a bone.
- Quitting faulted twice every time; it now takes about half a second.
- A long chat message or an unusual map name could kill the process.
- A map name the game cannot make sense of is refused as you start, instead of taking the game down later. A path with a space in it and no quotes around it left the loader walking the same step of that path forever, growing a string until it ran out of room, and the game then went on starting with half of itself missing. What you saw was a crash a second later, in the part of the game that runs a match, naming nothing that had anything to do with it. Any failure during start-up now stops the game there and says which one it was.
- Starting the game from the folder the zip was unpacked to says what is wrong. None of Zero Hour's own files are in that folder, so the first one the game asked for was missing, and the box that came up blamed viruses and overheated hardware. It now names the folder it looked in and tells you to run install.bat.
- A machine that once had Generals installed from a disc or The First Decade, and later removed it, no longer loses the original game's files. The old install leaves its address behind in Windows, the game trusted it over the copy Steam keeps beside Zero Hour, and the main menu came up over magenta water and black hills. A folder that turns out to be empty is passed over now, and when the original game's files are nowhere at all, a box says so and points you at Steam's file check instead of starting.
- Nor can a map handed to you over the network, however long a name the other machine gives it.
- An order too big for one packet arrives in pieces, and a piece that claims to belong outside the order is dropped instead of landing there.
- A chat line too long for a packet is dropped rather than arriving as a different, shorter line.
- The window no longer goes *Not Responding* during the menu's camera moves.
- Poison clouds, mine clearing, garrison kills and crew-killing weapons: all traced and fixed.
- Blowing up a full transport is survivable now, in every remaining variant.
- A unit can no longer load into a vehicle that no longer exists.
- Selling or losing an airfield under a jet on its takeoff roll no longer takes the game down. The jet takes off.
- A Particle Uplink Cannon fired by a script along a path no longer crashes at the path's last waypoint.
- A damage effect whose attacker died first no longer crashes the game, and neither does a flash-bang into a building with fewer soldiers than it was meant to kill.
- A disguised bomb truck losing its disguise used to rebuild itself onto the image it had just thrown away. It gets a fresh one now.
- A second exit order while a railed transport is still pushing a unit out no longer leaves the first one frozen half out of the door, unselectable, for the rest of the match.
- A unit that kills itself survives its own turn now.
- Sniping an empty bike, formation moves and hacker evasion no longer crash.
- An order naming a player who is not there is dropped instead of crashing.
- An order that names one selected unit when nothing is selected is dropped too, instead of reading off the end of an empty list.
- Subdual weapons work on a unit that is being healed. Healing could drive the subdual meter below zero and it stayed there, so the next stun gun had to fill a hole before it did anything.
- A stream of projectiles remembers which ones it fired. The list was cleared into a local of the same name, so the real one started as whatever was in the recycled memory.
- A unit that has to deploy before firing does it standing still. It used to set up wherever it happened to be when the target came into range, then pack straight back up because it was still on its way somewhere.
- An Aurora that went supersonic to attack comes back down again. The timer ran out and nothing put the normal engine back.
- Calling in a gunship selects it for the player who called it, not for everyone watching the match.
- A transport told to load into something it cannot enter - or into itself, which happens when it is part of the group you gave the order to - ignores the order instead of flying over and hovering beside it.
- A pilot ejecting from a wreck no longer plays a promotion sound and animation somewhere out in the map.
- A sound finishing does not take the game with it. Every sound carries a description of itself, and that description can be gone by the time the sound ends - it is dropped when the sound is renamed, cleared by hand when what it points at is about to be deleted, and absent on a sound that was queued to repeat after a delay. The 2003 code checked for that in two places and then read straight through it in fifteen others, one of which is where every finished sound goes. It crashed mid-match with a stack that is all audio and names nothing that caused it. A sound with no description is now simply not music and not speech, which is what all fifteen questions were asking; the channel it was using is still handed back, because losing one leaks a voice for the rest of the match and enough of them go quiet.
- A vehicle that gives off dirt but no dust no longer takes the game down when it lands. The landing
  puff belongs to the dust effect, and the code reached for it after checking that the dirt effect
  existed - so any vehicle whose data has one and not the other faulted the moment its wheels came
  back down.
- A file that appears after the game has already looked for it is found. The answer to "is this
  file there" was remembered forever, and nothing cleared it when a mod archive was mounted or the
  patch content was layered on top - so a file the game had asked about a moment too early stayed
  invisible for the rest of the session.
- A guard posted at a tunnel network no longer takes the game down when its target stops existing.
  EA's own copy of that function, the one for units guarding anywhere else, tests for it on its first
  line; the tunnel copy read straight through the missing pointer and crashed mid-match.
- Alt-tabbing out no longer kills the game if something needs loading while you are away. The
  simulation keeps running while the window is not drawing, so a script can drop reinforcements in,
  and the new unit's model asks for a texture that cannot be made because there is no graphics device
  to make it on. The failure came back as a success with nothing attached, and the next line of code
  used it. Two lines apart in the log: "Are we ALT-Tabbed out?" and the crash. A load that produces
  nothing is now a failed load, which the game has always known how to handle, and the texture is
  rebuilt when the device comes back.
- A bad line in a data file says which line. The startup error for an unparseable block named the
  line the block began on, which is almost never the line that is wrong, so the answer was "one of
  these nine fields, good luck". It now also reports the line it got to and what was on it.
- A crash inside somebody else's code now says whose. Every line of the stack that is not ours used
  to read "Unknown" and a bare address, so a fault inside a graphics driver looked exactly like a
  fault inside the game. Those lines now carry the file the address belongs to and the offset into
  it, which is the difference between a report that can be acted on and one that cannot.
- The game says so in the log when the graphics device is lost, which is what happens when you
  alt-tab out. The recovery after that is the riskiest thing the renderer does, and until now it
  left no trace at all in a shipping build.
- Wreckage no longer drops a shadow request full of whatever was on the stack. A shadow is asked
  for by filling in a small form, and the form has a slot for a texture name that the wreckage code
  never filled in at all. The shadow system reads that slot the moment its first byte is not zero,
  so it was measuring and copying a name nobody wrote. The form now starts blank.
- A battle thick with smoke, fire and glass draws instead of falling over. Everything see-through
  is collected, sorted back to front and drawn in one go, and the list it goes into can only count
  to 65,535 - which nothing checked. Past that the game asked for a buffer that cannot exist and
  wrote into it anyway. What does not fit now waits for the next pass.
- A map stored somewhere with a long name no longer overruns on the way in. The wave marks a map
  carries are looked up by swapping the map's own extension, and the name was copied into a fixed
  256-byte buffer first, with no check at either end.
- Saves, replays and settings find your Documents folder even when it has been moved. The shell call
  the game used writes into a fixed buffer and fails outright on a path longer than that, which is
  what a redirected folder on a network share or a long OneDrive path is. Everything then went
  somewhere else without a word.
- The game reads its own registry settings from your account before the machine-wide ones. Writing
  the machine-wide half needs administrator rights, so a per-user or Steam install only ever writes
  yours - and a stale entry left by some other copy of the game was winning, pointing this one at
  another install's language and data folder.
- Quitting no longer stops on a dialog nobody can see. An internal warning raised while the game is
  tearing down used to put a box up over a graphics device that was already going away.
- A log that could not be rotated says so, in the log. Rotation fails when a second copy of the game
  has the file open, and the ".prev" file next to it then holds some older run than the one before -
  which is a bug report read wrong.
- Something going wrong now writes a readable crash report. That includes the two cases that used to
  write none: a fault in the work the game does in the background, such as moving the cursor, and an
  error during loading that closed the game as if you had quit. The second one then set off the first
  on its way out, so the one report a player could send described the wrong crash. When the game the
  launcher started still closes on an error with no report, the launcher sends the exit code and the
  log on its own.

## Units take corners wide, and drive round a jam instead of into it

- A route used to be priced by the ground under it and nothing else, so a tank was handed the
  shortest line whether or not it could drive it. The shortest line scrapes every corner, and a
  tank that clips a corner stops, reverses and tries again while everything behind it waits. Routes
  are now charged for how close they run to terrain, and the charge climbs sharply in the last
  couple of squares, so a wide body swings out where there is room and only squeezes where there is
  no alternative.
- Squares where units are actually standing still are expensive for about a second afterwards. A
  unit that asks for a new route while stuck in a line is no longer handed the same line back: the
  queue in front of it costs something now, and a way round it that is not much longer wins.
- Routes also carry a clock. Each unit on the move says where it expects to be and roughly when,
  half a second at a time, and a route is charged where it wants a square somebody else wants at the
  same moment. Two columns following the same road are in the same squares at different moments and
  pay nothing for it; two columns crossing pay, and one of them goes round. That distinction is the
  whole point - the same map without a clock in it makes a shared road look as bad as a crossing.
- A group sent across the map plans its corridor from the member already closest to where you
  clicked, not from the one nearest the middle of the group. Planning from the middle made everyone
  ahead of it drive backwards to join, straight across the rest of the group.
- A route is now a width, not a line. Every unit used to steer at the exact middle of the ground it
  had been given, which is why twenty tanks sent across an empty field arrived in single file: they
  were all aiming at the same metre of it. Each unit now holds its own position across whatever
  width the ground allows beside it, measured where it is standing rather than fixed when the order
  was given, so an open field is crossed as a wide front and a bridge squeezes the same units back
  into a line without anybody deciding to queue. The width closes back onto the middle over the last
  few squares, so everyone still parks where you sent them.
- A group spreads into as many lanes as the road in front of it will take, and that number is now
  measured rather than assumed. It used to come from the widest reading the game can give, whatever
  the ground was: a dozen tanks on a two-lane road were handed a dozen lanes, every one of them
  wider than the road, and each was then cut back to the nearest verge - which is a column pressed
  into two files against the edges with nothing down the middle. The road is measured on both sides
  at the group's own feet when the order is given, so a wide field is crossed abreast, a lane road
  puts as many units side by side as it actually holds and the rest follow, and a route running
  along a wall lays its lanes out in the room that exists instead of into the wall.
- The position a unit takes across that width is the one it already had. A group you dragged a box
  round is spread out before it starts moving, and whoever was on the left of it stays on the left
  the whole way there. The spread is proportional, so a selection two hundred feet across arrives as
  a front rather than as two files pressed against the edges, and nothing is replaced by a formation
  nobody asked for. The unit keeps that line until it arrives; there is no slow drift back to the
  middle, which was the first version of this and was exactly what pulled a group back into single
  file a few seconds after it set off.
- A tank stuck behind a slower one now pulls out and goes past, if there is room beside the route to
  do it in. It holds the new line for a second and a half so it does not weave, and gives up a
  little speed while it crosses over. Nothing here costs a new route: the road is the same, only the
  part of it the tank drives on changes.
- Measured over twenty seven-minute four-way battles on the same twenty maps: time spent standing
  behind another unit fell 31%. Over six 4v4 battles, where every unit is on one of two fronts, it
  fell 38%, units left properly stuck fell by half, and the worst single frame of a match went from
  30.6 milliseconds to 9.9.
- A corner now costs what the vehicle takes to turn it. Every turn used to be priced the same,
  whether the thing making it pivots on the spot or needs a second and a half to swing a hull round,
  and the very first turn of a route was free: the one out of where the unit is standing at the
  moment you give the order. That is why a column ordered forward would sometimes set off by turning
  round, and why a tank was handed corners it had to stop, reverse and grind through. A turn is
  charged at what that hull actually takes now, and the first step is charged against the way the
  unit is already pointing. Twelve matches on the same twelve maps: time spent standing behind
  another unit down 38%, time spent properly wedged rather than merely queuing down from 11.3 to 0.3
  unit-frames a match, and the routing itself 20% cheaper to work out, because a route a vehicle can
  drive is a route it does not come back and ask to have redone.
- A group crossing open ground no longer arrives twenty abreast. Measuring the road is what stopped
  a dozen tanks driving down a two-lane street in single file, and with nothing capping it the same
  measurement turned an open field into a firing line wider than anything the group was walking
  into. Five lanes is the ceiling: still a front on open ground, still narrow enough to go through a
  base entrance without the whole group renumbering itself sideways at the gate.
- Attack-move spreads out the way a plain move does. It is the order that expects to be interrupted,
  and it was the one order that handed nobody a place in the formation, so a group told to fight its
  way across the map went in single file while the same group told to walk there arrived as a front.
- A group that has to find a new way round keeps its shape. Any new route used to drop a unit out of
  its formation permanently: it held its place until the first thing that made it re-plan, and drove
  down the middle of the road on its own from there on. Arriving is what ends a formation now.
- A unit stuck behind a unit that is never going to move asks for a different route. It tries both
  sides of the blockage first, and the outside of the whole pack, and where the road genuinely has no
  room it now stops steering around the problem and plans another way there. The squares the jam is
  standing on are already expensive by then, so the new route goes round it. A unit that has been
  getting nowhere for a third of a second also used to stop looking for a way past at exactly the
  moment it needed one; only the politeness stops now, and the looking carries on.
- Backing out of a wedge asks the queue behind to make room first, rather than reversing into it, and
  the unit plans a fresh route once it is out instead of driving back into the hole it just left.
- Units keep their distance from each other and not only from whoever is exactly alongside them. A
  neighbour half a body ahead and half a body over, closing, is the collision nothing used to see
  coming, and it is now worth a foot of road before it is worth a stop.
- Units no longer stop dead for traffic that was never going to hit them. Something crossing your
  path at an angle is not something sitting in your lane, and for one build it was priced as though
  it were: the brake reads how fast the obstruction is travelling along your route, which for a unit
  crossing it is nothing at all, so a jeep driving across a road brought the column on it to a
  standstill. Crossing traffic now costs a lift off the throttle, and only a unit genuinely in the
  way can ask for a stop.
- A unit turning on the spot is not a unit that is stuck. A heavy hull takes two seconds to come
  about and covers no ground doing it, which the jam detector counted as being wedged, so tanks
  halfway through a turn decided they were trapped and reversed out of a jam that did not exist.
- A unit aims at a point it can actually drive to. The steering point sits a couple of squares
  ahead and off to the side of the route, and it used to be checked against the width of the road
  where the unit is standing and the width where it is aiming, but not against the gap in between.
  Both ends being wide is not the same as the middle being wide, and the straight line between them
  went through the corner of the building the route had gone round. That is what catching on
  scenery looked like from the inside.
- Those three together, over eight battles on the same eight maps with forty units moved as one
  group every twenty seconds: time spent standing behind another unit down 7%, and no stutter added
  to the worst frames of a match. One map of the eight got worse, which is written down rather than
  averaged away.
- A unit that has stopped and wants to move is never left to it. The game now checks, once a frame
  and for every unit under orders, whether it is actually getting anywhere, and when the answer has
  been no for a second and a half it does something about it: first a fresh route from where the
  unit is standing, then telling whoever is crowding it to move and letting it push through, then
  backing the body out to open ground. If it is still stuck after all that, the whole sequence runs
  again rather than giving up. None of this waits for a collision, which is what the old machinery
  needed and what a unit stuck on scenery never produces.
- This is not a promise that units never get stuck. A unit walled in by buildings has nowhere to be
  sent, and no amount of steering makes ground that is not there. What it is is a promise that
  nothing is ignored: over twenty test battles, the longest any single unit spent wanting to move
  and not moving was 2.9 seconds in ordinary play, and 3.4 in a battle deliberately set up to jam.
  That number is measured at the end of every test run, so if it ever grows the run says so.
- Units that go back and forth on the spot are counted now, which is the first step to fixing them.
  A unit shuffling beside a building is moving on every single frame, so nothing in the game noticed
  it: the jam detector wants a unit that has stopped. Ground covered against ground gained over three
  seconds catches it instead, and about fifteen units a battle turn out to do it. Nothing is done
  about it yet on purpose. Three cures were tried and measured, and all three cost more than they
  saved: backing the unit out made everything a third worse, and forbidding it to change its mind
  for three seconds made it queue instead. The count ships so the next attempt has a number to beat.
- The plain version of the same question, asked of orders rather than of units: forty units sent
  across the map at once, checked twenty seconds later, 525 orders over eight maps. Of the units
  still carrying that order, one had not got going at all. Not one in a hundred: one. That is the
  figure this work is held to from now on, and every test run prints it along with the reason each
  stalled unit gave. Over those eight battles, no unit anywhere failed to move because no route
  could be found for it, and none was jammed against another unit for a whole twenty seconds.
- Jams in the heavy test fell 14% against the build from before all of this, and in ordinary play
  the whole of it costs nothing measurable: same number of routes planned, same time spent planning
  them.
- A tracked unit braking while sitting exactly on the spot it was sent to divided nothing by
  nothing. The result stayed in its brakes and was multiplied into the next push it was given, and
  a vehicle handed a number that is not a number goes somewhere no player asked for.
- A wedged tank backs out along its route, not towards the corner of the map. The check that
  decides whether a unit is getting anywhere, and the sideways and backwards escape it tries when it
  is not, both measured against a point that was always the map's origin. And when that escape asked
  for a new route, the request was wiped out in the same frame, so the route never came. Over twenty
  brutal battles on Twilight Flame, units spent 8% less time wedged and 4% less time blocked, with 14%
  more new routes actually planned. One of those twenty went the other way: a unit sat stuck for most
  of the match, and why is not known yet.

## Long orders stopped hitching

- The coarse pathfinder pass never worked, so every long move searched the whole map.
- A 23-cell route went from 55,000 cells and 256ms to 10,000 and 25ms.
- Every wall, fence and building permanently held one of the search's scratch records.
- Every abandoned search leaked one, and abandoned searches are the normal case.
- A recycled record could still point at the last unit that used it.
- A single unit lands where you clicked instead of the nearest square's middle.
- A footprint change no longer permanently blocks the ground beside a building.
- Our own rescue for narrow routes searched 140,513 squares in 285ms; it is gone.
- The coarse route is widened along its whole length, not just at the start.
- The main search stops after 20,000 squares and hands back a partial route.
- Long turns went from 513 to 201, and the worst from 2,976ms to 243ms.
- The distance guess now prices diagonals and turns, so the search actually steers.
- The straight-line shortcut only runs from a square closer than anything tried before.
- That one change: stuttering turns 615 to 108, pathfinding 11.8s to 1.6s.
- The queue is indexed, so adding a square costs one step instead of eighty.
- Joining two connected areas is one operation instead of a full table rewrite.
- Computer players think on staggered turns instead of all on the same one.
- Only one AI squad's march across the map is planned per turn.
- Map scripts, waypoints and regions are looked up directly instead of by scanning.
- Every effect knows its own place, so a burning building finds its smoke instantly.
- Decimal-to-whole-number conversion is one instruction instead of 1999 assembly.
- The minimap's fog is painted in memory and handed over once a frame.
- Minimap dots and terrain go over in one go, not one trip per dot.
- The terrain's lowest-point sweep is read along the grain now.
- A computer player choosing where to put a building could freeze the picture for a twentieth of a second. When the spot in its plan is taken it looks outwards for a free one, and on a skirmish map that search reaches most of the way across the terrain: three and a half thousand candidate positions, each a full check of whether a building fits there, all inside a single frame. It now walks one ring of that search per frame and carries on next frame from where it stopped, so it settles on the same spot it always would have, a fraction of a second later.
- An angry mob added up its members' health every frame, and so did every other mob on the map. Seventy-three of them in one frame cost 32 milliseconds - two frames of budget spent on a health bar and a rule about clicking. Each mob now does it five times a second, and they take turns, so the cost never lands on one frame.
- Those two, together with a third that has since been taken out again with the rest of the group movement work, over four seven-minute four-way battles: frames that missed a sixtieth of a second fell from 15 to 6, frames over a thirtieth from 3 to 1, and the slowest one frame in a thousand from 13.3 milliseconds to 10.5. The average is unchanged, which is the point - none of this was traded for anything.
- Every unit on the move re-checked the entire road ahead of it, every single frame. A tank sent across the map holds one long straight leg of its route, and each frame it asked whether that whole leg - three hundred squares of it - was clear, walking every square and testing a block of twenty-five around each one. That one question was a sixth of the game's entire running time. It now looks twenty squares ahead, which is thirty frames of driving, and asks again next frame like it always did. Units also drive better for it, not worse: over four battles the time spent standing behind each other fell 10% and the number left properly stuck fell from 296 a match to 10.
- Sending a group across the map could freeze the picture for a twenty-fifth of a second. The wide corridor a group travels down is its own search, the most expensive kind the game runs, and it was the only one in the pathfinder allowed to run without a limit - a three-unit team once spent 40 milliseconds on one. It now gives up after a set amount of looking and the group falls back to individual routes, which is what happens on ground too tight for a corridor anyway.
- The movement code was timing itself thirty thousand times a frame. Reading the clock that often cost 4.6% of the whole game to measure something already counted for free.
- Big fights run a quarter lighter. Every moving unit wrote out a line of text naming itself, every frame, for a sync-checking log that is switched off in every game anyone plays; the text was built and thrown away. In a 530-unit battle that was about a tenth of the game's time, and the average turn went from 3.9ms to 3.0ms.
- A driving unit worked out where to steer from scratch every frame, checking twenty squares of road ahead and a block of twenty-five around each square. The answer now holds until the unit has covered half a square, and it is still worked out fresh every frame near the end of the route and near a corner. Units also stamped their planned route a dozen seconds ahead over a five-square-wide strip, so other units could avoid busy crossings; the strip is three squares and about six seconds now. Together those took the same 530-unit battle from 2.9ms a turn to 2.4ms. Twenty tanks pushed through a ramp on four maps arrive within 7% of the time they took before any of this, and none got stuck.
- A computer player that had lost the building that makes dozers searched the whole catalogue of units for one it could build, once for every missing building in its plan. A late eight-player match did that 33 times every two seconds, for 13ms. It now asks once per turn.
- A computer player looking for room beside a crowded base could hold the game at 20 to 37ms a turn for eight turns running. Its search was meant to stop after 32 spots a turn, but it only looked at the limit between rings of spots, and a ring far from the base holds more than 200. It stops at 32 wherever it is now, picks up at that spot next turn, and settles on the same place it always did.
- Choosing which team to build next scored every team the computer could make against every kind of enemy unit it could see, working out from scratch how long each would take to kill the other. One such choice in an eight-player match took 84ms. Those answers depend only on the units' own data, so each is worked out once per match and remembered.
- A route got straightened by testing every point on it against every later point. The limit of 32 points ahead had been lifted for an experiment and never put back, so one long route could cost 12ms. With it back, a 40,000-frame match spent 4ms on straightening where it spent 1.7 seconds, and turns slower than a thirtieth of a second went from 8 to 1.
- Single units no longer freeze the game for a tenth of a second at random. The game's diary was forced out to disk after every line it wrote, and a gunner holding fire wrote one every time it waited, 104,000 of them in one match. That line is gone, and the diary is written out once a second, and in full if the game ever crashes.
- The queue that answers "where do I walk" was allowed to answer as many units as asked in the same instant. Ordering a group, or a fight breaking out, would drop ten questions into it at once and one frame would answer all ten. It now answers three a frame, which is still many times more than a match ever asks for on average, and the rest wait a frame. Units move better for it as well as the game running smoother: the number of routes computed in a whole battle fell by a fifth, because a unit whose answer comes one frame later usually no longer needs the second and third answer it used to ask for while stuck in traffic.
- The four together, on the same four battles: the average logic frame is a fifth shorter, the worst frame of a match went from 38.6 milliseconds to 16.0, frames over a sixtieth of a second went from 2 to none at all, and the number of frames costing more than 3 milliseconds fell from 14,105 to 3,360. In an eight-player 4v4 the average frame is a tenth shorter and the worst one went from 26.7 milliseconds to 17.9.
- A unit told to hunt looks across the whole map for something to shoot. That search stepped through every patch of ground on both sides of the unit, most of it past the edge of the map, and asked "can I hurt this one" of every enemy it passed. One hunter in an eight-player fight could hold the game for a thirtieth of a second. It now walks the list of units that actually exist, and only asks that question of an enemy that would be picked. It picks the same target, and hunting no longer shows up among the slow frames at all.
- A computer player planning its base checked whether every unbuilt spot in its plan was safe from the enemy on every pass, including spots it was never going to pick that pass. Thirty-six of those checks took 28ms in one frame. Only a spot that could be chosen gets checked now.
- A computer player expanding to a far supply dock ordered a second supply centre there every minute until the first one stood, and searched the ground around the dock for room each time, 23 to 33ms a go. It now waits for the one it already ordered.
- All of it together, on the same two eight-player 4v4 battles of 30,000 frames that opened this section: the slowest frame went from 150 and 267 milliseconds to 42 and 26, and frames longer than a thirtieth of a second went from 13 and 8 to one. Two four-player battles went from 132 and 65 milliseconds at worst to 30 and 26, with none over a thirtieth.
- Putting a building on uneven ground froze the picture for a sixteenth of a second. Levelling the dirt under a foundation asked the game to re-light every blade of grass on the map - sixty-one milliseconds, measured, for a patch a few paces wide. It now redoes the patch. Over a seven-minute four-way battle that took the worst frame of the match from 82 milliseconds down to 19, and left nothing at all above a thirtieth of a second.

- The rest of the same mob. A mob is a leader and ten rioters, and a rioter only reconsiders where it is standing once every sixteen frames - but it was woken on all sixteen anyway, just to be sent back to sleep, and every mob on the map is eleven of those. EA left a note in the code asking for exactly this and never came back to it. The rioters sleep properly now, and one whose mob is dead stops being woken at all. Eight mobs on the field is eighty rioters: where all eighty were woken every frame, five are. Measured against the number in the complaint rather than a comfortable one: a hundred mobs is 1,100 men and costs 4.3 milliseconds of a 33 millisecond frame, two hundred and fifty is 2,750 men and 7.1, four hundred is 4,400 men and 12.6, and not one frame of any of those three ran over budget. Four hundred mobs is past anything a match will produce; what the rioters cost there is a fifth of a millisecond.

- The first soldier out of your barracks cost eighteen milliseconds of a thirty-three millisecond frame. A unit's models were read off the disc the moment one of its kind first appeared, and that read landed on whatever frame the unit happened to walk out on. So did the first war factory, the first supply centre, the first of everything. That is why the opening of a match was the part that jerked: the opening of a match is nothing but things appearing for the first time. Everything a match can put on the field is now read during the loading screen, which costs about three seconds there and takes the hitches out of the game. EA wrote that loading pass and left it switched off; the code carries their own comment saying it exists "so that we don't have big pauses when building those objects".

## Sound, video, and getting it to start at all

- Audio is real, through the audio library the retail game ships with.
- A death cry plays to the end. So does "construction complete", and the tail of a gun. Telling a sound to stop used to cut it off that frame, where the 2003 game let it finish and only kept it from repeating; our own fix for the audio thread broke that, and it is put back. A looping sound caught in the pause between two plays can be stopped too, instead of carrying on after whatever made it is gone.
- Movies follow the voice volume slider. The volume was handed to the video before it was ready to take it, so every movie played at full level, and moving the slider while one was playing changed nothing.
- A sound at the edge of its range can fade out instead of stopping dead. The volume curve never
  actually reaches zero, so a sound you are walking away from was cut off mid-note at the maximum
  range. `RangeVolumeFade = Yes` in `AudioSettings.ini` uses a fade that reaches zero exactly where
  the cut is. Off by default, because it changes how every positional sound in the game carries.
- The videos play: the intro, the sizzle reel, the mission briefings, the general portraits.
- The pointer is on screen over them. It used to appear only once the main menu did, so clicking through the logos was done blind.
- Escape skips the opening logo, not just everything after it. The key has always skipped movies; the gate it asks was raised only once the logo had played itself out, so the one film you see on every single launch was the one you could not get past.
- The graphics go straight to a modern path. About 5,600 calls used to be translated on the way out
  by a small library shipped alongside the game; the game now speaks that path itself and the extra
  library is gone from the download. The picture is the picture it was, and the frame it draws in a
  fixed-seed match is within half a percent of the old one, pixel for pixel.
- Vertical sync works in a window. The option has always been in the menu, and in a window the old
  path ignored it and ran the picture as fast as it could, which is what a screen tears from. It is
  honoured now, so the picture holds to your monitor's refresh rate unless you turn it off.
- No disc, no registry keys, no retail installer â€” a normal install works.
- The startup screen is this build's own, with the Zero Hour Reforged name on it, so you can see which one you launched before the menu loads.
- The main menu buttons are there when the menu is. On the first visit they used to stay hidden, pointer and all, until you nudged the mouse or pressed a key, so a freshly started game looked stuck on the background battle. A match started straight from the command line no longer plays those buttons sliding in over its loading screen.
- The zip installs itself and takes itself back off. `install.bat` asks where the game is, offering
  whatever the registry says, and copies the build there; anything it is about to write over goes
  into a zip in `ZHReforged-Uninstall` first, and anything it adds that was not there is written down
  so it can be deleted rather than restored. `uninstall.bat`, left in that same folder, puts the
  install back the way it found it. Installing a second time on top does not bury the first backup:
  it keeps the copy of the files as the retail game had them, not as the previous build left them.
- The audio and video libraries travel in the zip now. An install that was missing `mss32.dll` died
  in a Windows dialog naming the file, before the game ran a single line of its own and with nothing
  written to any log; the exe asks Windows for that library by name at load, so there was no way to
  read the error as anything other than a broken download. The audio library's own output drivers go
  with it, since it looks those up in a folder while it is running rather than at load, and an
  install short of them starts and then plays nothing at all. All of it is the retail files, and the
  old copies are backed up like everything else the installer writes over.
- The game no longer needs a Visual C++ redistributable installed. The exe asked Windows for two
  support libraries by name at load, so a machine that had never installed one failed exactly the
  way a missing `mss32.dll` did: a dialog naming a file, nothing written to any log, nothing to send
  back to anyone. Both libraries are compiled into the exe now. The
  download is half a megabyte larger and there is nothing left to install alongside it.
- Over a build that is already there it updates rather than installs, and says which number it is
  moving you from and to. Running an older package over a newer install stops instead, since the
  usual reason for doing that by accident is having two zips in the downloads folder; the word
  force on the command line goes back anyway.
- The 1.04 patch content is reachable again.

---

## How this was done

- Ported leaf-first: every library compiled, tested and green before its dependents.
- 14 automated suites; most bugs above were found by tests, not by reading code.
- The game plays itself: eight computer opponents from one command line.
- An opponent can be set to Human in the skirmish screen: a base with no brain behind it.
- Its sight is yours from the first frame, and clicking any of it hands you the base itself.
- Shift-Ctrl-T does the same to any opponent, a computer one included, and walks around the table.
- Headless, a 23-minute skirmish plays out in 38 seconds, identically every run - and opens no window at all, so twenty of them in a row leave the desktop and the keyboard focus alone.
- It plays itself over a network too â€” two copies, one real connection.
- That found every multiplayer replay falsely accusing itself of desync since 2003.
- Every fix was proved by putting the bug back and watching the test fail.
- No debugger here: a crash symboliser, a sampling profiler, probes in live matches.
- A graphics fix is argued with pixels: an unattended match can now be told where to point the camera and which frame to photograph, so the two builds are compared by counting the pixels between them. The river above changed 126,535 of them and the eye had been calling it "about the same".
- A picture cannot show a unit turning the wrong way, so an unattended match can now film itself: name the first and the last game frame and every frame between them is drawn once and saved, then stitched into an mp4 at the game's own 30 frames a second. A replay films the same way, which puts any match ever recorded on video without anybody sitting through it.
- And a match can be shot like a film. Give it a shot list, a text file of lines like "at 16 seconds, fly through these three points over 8 seconds" or "at 4 seconds, ride with the nearest Crusader", and the interface, the cursor and the health bars leave the screen while the camera cuts, glides along a curve, orbits, tilts, closes in and chases a unit on its own, with widescreen bars if you ask for them. The camera keeps time by the game's own clock, so the same list over the same match frames the same picture on every run, and with filming switched on the footage comes out at the speed it was written for, however slowly the disk took each frame.
- A unit's cost is argued with the same army twice. An unattended match can now take its army from a text file - spawn eight of that unit there on frame thirty, send them across the map on frame six hundred, stop them on frame nine hundred - and be told which faction each seat plays and to leave every seat without a computer player behind it. Before that, whatever was on the field came out of the seed: measuring what angry mobs cost meant waiting for a match that happened to build some, and two runs that drew different sides were never comparable in the first place.
- And the other half of that: a running game will now take orders down a socket. Started with the switch for it, the game listens on this machine only and answers anything that can speak to a web page - start a match on this map with these players, put five of that unit there, send them across the map, take a picture, tell me the frame number and what everybody owns. The commands are the same lines the text file above takes. A twenty-line Python script can now set up a match, produce an army and read the result back, which is how the mob numbers in this list were produced.
- Reverted and recorded: wide FOV, the whole group movement rework, tree shadows out of a stencil
  volume. That last one was a day spent proving the models cannot do it: a tree is a flat two-sided
  sheet with no closed silhouette, so a palm threw its trunk and a leafy tree threw nothing. The
  shadow trees have now is the opposite trick, and cost about thirty lines: the batch of triangles
  the trees are already drawn from, drawn a second time with the tree laid flat on its own base and
  slid along the sun. The shape comes free because it is the same triangles and the same texture.
- Soldiers used to drop their posed shadow once the camera pulled far enough back to make each man a
  few pixels tall. It saved frame time in a big crowd and it made the army look flat at the zoom
  most games are played at, so it came out again: every soldier on screen keeps his shadow.
- The six-rung difficulty ladder is back down to three. The three extra rungs were built, played and taken out again: a player picks a level once and wants to know what it means, and six names that each moved one switch was a worse answer to that than three that each describe an opponent. The machinery underneath is the same, so the levels are still tunable in the data files, and Hard kept the top rung's numbers rather than its old ones.
- The three-piece command bar was reverted once, for having nowhere to put the painting of the bar, and is back now that the painting has been cut into three to match. Each piece is fitted by matching it against the artwork it was cut from rather than by eye: the eyeballed fit was four percent out, which nobody sees on the metal and everybody sees on the money readout.
- The opponent's decisions are argued with a number: 20 headless matches per change, same seeds, win rate and match length before and after.
- That caught two changes that looked right and measured catastrophic - a wave that waited jammed the whole production line, and a retreat rule that counted buildings as gunfire sent every attack home. Both showed up as twenty matches with zero kills.
- The same habit caught a speed fix that measured beautifully for the wrong reason. Capping the search for room beside a supply dock cut a once-a-minute stall and dropped the average eight-player frame below a millisecond, which was the giveaway: the first supply centre of every match went down on a blocked spot and never went up, so no computer player had an economy and nothing was happening. Counting supply trucks and attack waves next to the frame times showed it at once. The cap came out, and the stall was cured instead by not ordering the same supply centre twice.
- Massing an army before attacking is written and measured but switched off: it needs somewhere to wait that is not the production queue.
- The whole of the group movement work is out of the game again, and that is the largest single thing this list has taken back. Lanes priced by how many other units already plan to drive over a square, a unit giving up on a queue and planning its way round, the shared corridor a selection travels down, routes priced by when somebody else will be standing on a square, spacing measured off the units you selected: all of it built, all of it measured, all of it removed. What sank it was that the self-play harness cannot see a traffic jam. A computer opponent moves five units at a time and never forms the queue a person makes by dragging a box round fifteen tanks, so thirty-three of forty-eight maps came out bit-identical over ninety-six matches and the rule fired about twice a match. Then it was asked to swing wider, and four ways of doing that were tried and thrown away - two changed nothing at all, two made units wait longer than doing nothing. Overtaking went the same way over forty matches, with the time units spend properly stuck moving in the wrong direction, which is the one column it had been rebuilt to protect. EA wrote a version of that in 2003 and switched theirs off too. The speed work underneath it stayed; the behaviour is retail's again, and this is a problem to come back to with a way of measuring it that a machine opponent cannot flatter.
- Bugs deliberately left alone are pinned by a test documenting the behaviour.
- Infantry shadows were fixed in the wrong place first, and nobody has eyeballed them yet.
- The shade under smoke vanished between builds and was rewritten from these notes.
- The missing tree shadows were found by painting them red, not by reasoning.
- Zoom toward the cursor was fixed twice and is argued from code, not watched.
- The frame was finally measured rather than guessed at: 2003's own stopwatches were switched back on for a separate measurement build, and a seven-minute four-way match with two hundred and seventy units on the map was timed scope by scope.
- That killed a plan. Spreading the shadow work over sixteen cores would have bought two percent of a frame; particles, four hundredths of one percent. Nine tenths of the time is spent handing triangles to the graphics card, which no amount of threads makes faster. The plan is written down, with the numbers, so nobody spends a fortnight rediscovering it.
- One thing did come out of it: sixteen thousand five hundred lock operations per frame, all on the same lock, every time a scrap of memory is taken or given back. That is the next thing worth chasing.
- Then the question changed from "how fast" to "how steady", and the stopwatch had to change with it: an average is exactly the number that hides a stutter. The game now keeps the shape of every frame it draws and reports the worst ones, with the name of what took the time. The building-foundation freeze above was found that way, in one run, having been in the game since 2003.
- The self-play harness can set up teams now, which it never could: every computer opponent used to fight every other one. Eight players in a free-for-all spread the fighting over the whole map, and that is not the load anybody complains about - 4v4 puts every unit on one of two fronts, which is where they bunch up. That is now one switch, and it is how the numbers above were checked at the heavy end.
- A second pass with the same tool named two more, and the log did the work each time rather than anyone guessing: the computer player's search for somewhere to build, and the angry mob's health count. A third is named and not fixed - a computer team sent across the map on its approach path spends 40 milliseconds finding the wide corridor for it, once or twice a match, and that is one search rather than a mistake repeated. It is the next one.
- Group movement was asked a third time and this time answered yes, from a different direction. The
  version that was thrown out priced squares by how many other units planned to drive over them, and
  the harness could not see it work because a computer opponent never forms the queue a person makes
  by dragging a box round fifteen tanks - thirty-three of forty-eight maps came out bit-identical.
  What went back in is priced by terrain instead, which is on every map on every frame whether or
  not anyone is queuing, and the traffic and crossing costs ride on top of that rather than carrying
  the whole change on their own. Every route in every match changes, so there is something to
  measure: twenty four-player matches, same twenty maps, time spent standing behind another unit
  31% lower; six 4v4 matches, 38% lower with half the properly-stuck frames.
- It cost search time and that had to be paid for. Adding a charge the distance estimate cannot see
  is the same mistake the pathfinder was rescued from two years ago: the search stops walking toward
  the goal and fans out instead. First measurement, twenty matches, 225,419 squares looked at per
  match against 57,596 before - four times the work. Raising the estimate to match what the charge
  adds brought it back to 90,058, and with it the units left properly stuck, from 43 a match to 8.
  What remains is 56% more searching for 31% less waiting, and on the heavy 4v4 load the worst frame
  of a match got better rather than worse, because the search work spreads over frames and a traffic
  jam does not.
- Every knob in it is one switch away from off, and the switch is in the same executable, because a
  batch of matches only argues something if both halves of it were built by the same compiler on the
  same afternoon.
- Group movement was also asked one more question and answered no, then asked it again with one piece changed and answered yes. The idea was to stop treating the route as a line and treat it as a band. The first version had every unit drift away from whichever side ahead was busiest; that was built in the movement sandbox, measured against the columns the game ships over 96 layouts one at a time at four drift speeds, and it was worse at every speed and steadily worse the harder it pushed. What was missing was a reason to move sideways at all beyond a crowd reading. The version that shipped keeps the band and throws the drift away: a unit changes its line only when something slower is actually in front of it.
- The first attempt at the band did nothing at all, and looked like it worked. Each unit was to take its line from its own sideways distance to its own route, which is zero for every unit that ever existed: a unit's route starts under its own tracks. So every one of them came out on the centre line, the band was measured correctly and nobody was ever put anywhere in it, and the only thing left moving units sideways was the overtaking rule. It only showed up on screen, twenty-five tanks still collapsing into one column with all the machinery switched on. The line now comes from the group that gave the order, which is the only thing that knows the group has a shape, and it is handed down as a fraction of the group's own width rather than in feet, so a wide selection maps onto a narrow road instead of everybody piling up against the two edges.
- Paired with that, the drift back to the middle came out. It moved every unit 0.8% of the way to the same line every frame, which over four seconds is the whole spread, and it was fighting the thing it shipped alongside. Arrival is already handled by the band closing over the last few squares.
- The charge for driving through a queue was then doubled, to the weight the sandbox this was designed in opens at, and the game got worse at exactly the thing the charge is for: twelve four-way battles, time spent standing behind another unit up 64%, five of twelve maps worse and none better, one of them from 770 unit-frames to 2,968. Search got cheaper doing it - a tenth fewer squares looked at, a sixth less time - which is the tell: the routes are being decided sooner and further away from the queue, so a unit commits to a detour before it knows whether the queue was worth avoiding. The two weights are not the same quantity either, and calling them both four is a coincidence of naming - the sandbox's traffic value grows without limit and fades, the game's is capped. It is one number to put back. Measured again after the band was widened and re-anchored, it is still the wrong number, and now for a reason worth writing down: on the twelve-match average it looks 20% better, and seed by seed it wins one map and loses four. The average is one map - a single battle that went from 4,389 unit-frames of standing still down to 1,563 - carrying eleven others in the other direction, and on one of those the units that jammed against each other and both stopped went from 7 to 129.
- The band is not free of doubt, and the self-play harness is close to blind to it. Twelve four-way battles with it on and off: time spent standing behind another unit identical to three figures, the worst single frame of a match 69ms down to 28ms, and units left properly stuck 0.8 a match up to 2.9. That last column is the one the earlier overtaking attempt was thrown out over. A computer opponent moves five units at a time and never drags a box round twenty-five tanks, so the case the band is for is the case the harness does not play - the same reason the first group-movement attempt could not be measured. Both the band and the passing are one switch away from off, and twelve matches is not a result.
- With the band finally visible, it turned out to be three tanks wide. Measured over a four-player match it came out 77 feet across, and a tank is 24 of those, so a selection of twenty-five had nowhere to go and squeezed back into a column the moment they touched. The ground is now measured twice as far to each side, and the line each unit is given is a distance rather than a share of the group: members are sorted across the direction they are travelling and spaced by the width of the largest body among them, with the count of lanes capped at what the widest band can hold and everybody else queueing behind in order. A tight blob and a wide box now leave in the same shape, and a doorway still puts them back in single file because the offsets scale with the room actually found.
- Widening it exposed a second mistake, worth 10% of the time units spend standing still: the middle of the band was the middle of the free ground rather than the route itself. On a road with a cliff one side and a field the other, a unit that had never been given a line at all slid up to seventy-five feet off its own route. The two halves of the band are now measured separately and meet on the route. Twelve four-way battles: time spent blocked down 34%, from 770 unit-frames a match to 464.
- Watching the wide band in game found three more things wrong with it, and fixing them took time spent standing still down another 30%, from 464 unit-frames a match to 324, with units left properly stuck against each other down from 11.7 a match to 2.7. A tank reaching a corner used to stop there and rotate on the spot: its line was being measured sideways to the direction it came in on, while the point it was steering at had already moved past the turn onto the next stretch, so on a sharp bend the game was aiming it at somewhere behind itself. The line is now measured against whichever stretch the steering point is actually on, and any offset that lands behind the unit is thrown away rather than driven at. A group ordered onto one spot used to arrive spread across it instead of gathering on it, because the band only finished closing at the goal itself - it now shuts thirty feet short of it, and starts closing fifty feet earlier to have room to do it in.
- The third one is why a jam stayed a jam. A unit only tried to go round something in front of it if that thing was slower than it, and it asked the question by comparing engines rather than speedometers - so a column of identical tanks nose to tail, every one of them crawling and every one of them reporting full speed, never passed anybody at all. It is the actual speed now, which is the one case the passing rule was written for and the one case it refused to fire in.
- Closing the band through corners was tried at the same time and thrown out: scaling the offset by how sharp the bend is sounds right, and it more than tripled the time units spend blocked, 32 unit-frames per 1000 to 102 over twelve battles. A route bends constantly, and a band that shuts at every bend is single file for most of its length.
- A group you send somewhere now travels as a crowd rather than a queue, behind its own switch. Each unit holds a measured line across the road instead of the middle of it, moves over for a bigger unit coming up behind, pulls round something slower in front when there is room beside it, spreads out at the back of a jam and closes up again when it clears, and slows down only for traffic it is actually catching. It applies to orders given to more than one unit, because a single vehicle repositioning inside a firefight has nobody to share a road with, and the rules cost it real time when it was included: with every unit in the game steering this way the time spent standing behind another unit came out at 108 unit-frames per 1000 rather than 29.
- Two things the crowd did that no measurement could see, both of them tanks rotating on the spot instead of driving. Moving over for someone names a line a full body away, and taking it in one frame swings the point the unit is aiming at some twenty-five feet sideways two squares in front of its tracks, which is not a turn a tank can make: it stops, rotates, and by the time it is facing the new line the unit it was moving over for has gone past. A unit now slides into its new line at a quarter of the speed it is driving, which is about twenty degrees of steering. The second was the aiming point landing behind the unit - shoved out of a queue, or carried past its own lookahead through a bend - and the tank turning round and driving at it, which is the rocking back and forth. The point is now walked forward along the route until it is genuinely in front. The self-play harness reports both of these as no change at all, to three figures across sixteen battles, which is what a fix to something only the camera can see looks like.
- Two more places where the crowd was told the right thing one frame at a time and shook itself apart doing it. Keeping out of a neighbour's way is measured every frame against whatever is nearest, and whatever is nearest changes the moment two units cross, so the push each unit felt jumped from one side to the other between frames and the tank sat there twitching. The push is now averaged over about a quarter of a second, which is longer than the swap and shorter than anything a player can see. The other is the steering itself: a tank was being handed a new heading every frame with no memory of the last one, so a hundred small corrections became a wobble down the length of a column. It now turns towards the point it wants at a fixed rate rather than snapping to it, a quarter of the difference per frame while it is cruising and two thirds when something is actually in its way, because a unit dodging a collision cannot afford a smooth turn. Under two degrees off it holds the wheel still instead of chasing the last of the error, which is the difference between a tank standing still and a tank trembling.
- The one tank in thirty that stops for good now gets itself out. A unit that has asked to move and covered no ground for two seconds is not in traffic, it is wedged: between two allies it never quite touches, in the corner of a cliff, behind the one vehicle in the column that is never going to move again. Nothing counted it as blocked, because there was no collision to count, so nothing came to its rescue and it sat there for the rest of the battle. It now measures its own progress against the speed it asked for, and the answer has two rungs. A third of a second of getting nowhere and it stops being polite: no giving way, no moving over, no braking, no spreading out, all of which were costing it the speed it needs to push through. Two seconds and it backs out, to the side and behind, and tries the road again from there. Failing that it turns round. If it is walled in on all four sides it keeps trying and asks again a second later.
- Moving over for someone and joining a road are the same manoeuvre from two sides, and the crowd now does both. A unit coming in at an angle used to be invisible until it was in front of you, at which point everybody braked; the units already on the road now see it crossing their line up to a second and a half ahead and shift a lane over instead, which is what turns a queue into a zipper. The test is a real closest approach rather than a cone, and the courtesy is only extended to traffic that would have made you brake anyway - anything looser and a column spends the whole march shuffling sideways for vehicles driving vaguely alongside it.
- Three ways of building that were measured and thrown away, one of them expensive. Slowing down by how far away the traffic in front is, rather than by whether it is being caught, costs 3518 unit-frames of standing still a battle against 1236. Slowing down behind a unit that is not moving at all costs 16593, and it is the worst kind of bug: braking short of a parked ally means the collision never happens, so nothing counts the unit as blocked and none of the machinery that would have sent it round fires. It dies politely, three feet behind a tank that is never going to move. And standing the rules down for short trips, on the theory that a four-square shuffle inside a fight is not a march, is three times worse than leaving them on: short trips are exactly where units are packed tightest.
- Light vehicles could not hold a straight line: the head of a scout shook the whole way down a road an Overlord drove dead straight. The point a unit steers at sat two cells and a body ahead, taken from whichever sample of the route it happened to be beside, and both halves of that were wrong for a fast chassis. Samples are a cell apart, so the point jumped ten feet forward each time the unit crossed one and the heading jumped with it; it is taken between samples now, off the unit's own unrounded distance along the route, and slides forward a foot at a time. The other half is that thirty feet of warning is forty frames of road for an Overlord and ten for a scout, so the light stuff was steering at something under its nose where a foot of error is a full lock. Two ways of fixing that second half were tried and both cost more than they were worth. Moving the point further out for fast vehicles cured the shake and left four times as many units stuck: a point fifty feet up the road is measured against the ground fifty feet up the road, and nobody asks about the narrow bit in between. Slowing the wheel for them instead - turning towards the new heading at a rate cut by how much road the chassis eats in half a second - was better mannered and still cost nearly four times the stuck units. Lag on the wheel and a point too far ahead fail the same way: both answer where the unit was a moment ago. The sliding point cures the shake on its own, so that is all that is in.
- Vehicles walked off the side of bridges and queued up beside the entrance to them instead of driving on. A unit driving in a crowd holds a line a set distance off the middle of the road, and the road's width is measured by asking the ground either side whether a tank fits on it. On a bridge that question was being asked of the riverbank underneath. So the road read as sixty feet wide over a deck barely wide enough for two tanks, and a line held a body width off centre walked a column into the water - and on the approach, the same line put the leading tank against the abutment rather than the entrance, with everybody behind it stopped on the bank. The width is now measured on the deck the route actually runs over, and both the bridge and sixty feet of road each side of it have no width at all: every unit drives the middle of the road across a bridge, which is what the crossing is for. Coming at the entrance from an angle used to jam anyway, and that was the rescue rule doing it. A unit that has been getting nowhere for two seconds backs out sideways and tries the road again from there, and the riverbank beside an abutment is perfectly good ground to back out onto - so the vehicle left the only queue that leads onto the bridge and then had to fight its way back into it. On a bridge and its approach it now backs straight up instead, and keeps its place.
- Infantry can walk through infantry again, and a squad arrives as a squad. Soldiers have always been able to share ground in this game, and the crowd rules had quietly taken that back: a platoon sent across a field pulled itself into a rank sixty feet wide, every man of it braking, giving way and shuffling sideways for a crowd he was supposed to walk straight into. Foot soldiers are now outside the crowd model on both sides of the question. They keep to one line and follow it, and a tank no longer brakes for a rifleman it is about to drive through.
- A tank that cannot get past goes round on whatever room there is, rather than settling in behind. Two things used to send it to the back of the queue when a foot of road either side would have let it out. It would not change lanes within a second and a half of the last time it changed lanes, which is exactly the vehicle that just gave way to somebody and is now stuck behind the next one along; and it wanted six feet of air beside the vehicle it was passing or it would not go at all. A unit that has been queueing for a third of a second now asks again regardless, and settles for a body and a half of air if that is what the road has. The comfortable pass is still tried on both sides first, so nothing squeezes past where there was room to go round properly. Units also start spreading out at the back of a jam after a third of a second instead of a full second.
- A tank going round now goes round the outside of the crowd, not into the middle of it. The pass only ever asked whether the road reached the spot beside the vehicle in front; it never asked whether anything was standing there. In a group six abreast every such spot is another tank, so the unit aimed at one, was shoved back out of it, and queued inside its own group - which is what it looked like on screen. It now knows where its neighbours sit across the road, refuses any line one of them is already on, and has the outer edge of the whole pack as a third choice between the comfortable pass and the tight one. With a single vehicle in front the outside of the pack and the tight pass beside it are the same line, so an ordinary overtake is unchanged.
- A group is now as wide as the road it is on, and it works that out the whole way rather than once. The number of lanes was decided at the moment the order was given, from two measurements taken where the selection happened to be standing - so a dozen tanks leaving a base through a gate were handed one lane and drove the next thousand feet of open ground in single file, because nothing asked the question a second time. What the order hands out now is a place in the rank: which vehicle you are from the left, out of how many, at what spacing. How many of those ranks fit is answered every frame against the ground beside that vehicle. The column closes to single file in the gate, opens to six abreast on the far side, and closes again at the next gate, and none of it is a decision anybody makes. Widening asks for half a lane of room to spare before it takes a rank, which matters more than it sounds: the raw count is a floor of a measurement that wanders, a stretch of road a hair over five lanes wide reads five, four, five, four, and every flip renumbers the whole group and sends half of it half a lane sideways and back for the length of the road. Narrowing is immediate - when the ground is gone it is gone. The ceiling on how much road can be seen either side went up with it, from a hundred feet to a hundred and sixty, which is what every other width measurement in the game already used. Measured over thirty-eight battles: units left properly stuck fell from 6.0 a match to 1.3, and time spent standing still fell 2.4% with it.
- None of the crowd was being measured, and the numbers written here for it were another rule's. A unit steers as part of a crowd only if it was handed a line across the road, a line is handed out only by the order a player gives to a selection, and a computer opponent never gives that order: it moves its teams one vehicle at a time. So every self-play batch run to argue about the crowd was driving the two rules that live in the collision code and none of the steering. The harness now has a switch that hands every army on the map one group order every twenty seconds, to alternating corners. The match it produces is nonsense and its result says nothing, but it multiplies the time units spend standing still by fourteen, from around 300 unit-frames a match to 4,200, which is the proof it is finally creating the traffic the crowd was written for.
- With the traffic there, the crowd steering does not pay for itself. Thirty-nine battles paired map by map: time spent standing behind another unit 4,444 unit-frames a match with the rules off against 4,787 with them on, 7.7% worse, eleven maps better and twenty worse. The middle map moved by 19 unit-frames either way, so that average is four bad maps carrying thirty-five that barely noticed. What it does fix is the thing it was asked to fix. Units left properly stuck fell from 3.0 a match to 1.4, and the maps that moved are the pathological ones: 62 units wedged for good down to none, 35 down to none, 7 down to none. It creates new ones elsewhere, one map going from 5 to 34. The switch stays off, and what needs explaining now is the four maps, not the average.
- A network game now runs the shipped rules no matter what was typed to start it. Half a dozen switches exist so that the same build can be run twice and the two compared, and every one of them changes what a unit or the computer opponent decides: the crowd model, the three that put a piece of the movement back the way it shipped in 2003, how often a unit thinks, and the truce clock. Two players deciding differently is a match that comes apart on the first order rather than one that refuses the join, so online they are all off and the host's lobby settings are the only settings. Anyone who turned one on to look at it can still play with anyone who did not. A replay keeps whatever it was recorded with.
- Then the switches themselves went, because a switch that changes what a unit decides is a way to start two copies of the game that play different matches. The crowd steering is no longer something you start the game with: every match drives with it now, the computer's armies included. The three that put a piece of 2003 movement back are gone and the movement they could turn off is always on. The group drill that measured the crowd went too, and so did the setting for how often a unit thinks, which nobody had ever measured.
- A group no longer drops to half speed at every corner of its route. The crowd steering slowed a vehicle to as little as 55% wherever the road turned, on top of the slowing down the tank already does for a turn it cannot take, and a pathfinder route turns all the time: a quarter of the steering on a twenty-tank test sat under it, and a tank with the road to itself averaged 92% of its speed. Without it the same twenty Crusaders cross the same ramp to the enemy base in 71 seconds instead of 86, three seconds quicker than with no crowd steering at all, and the time they spend stuck behind each other fell by a third. The truce clock and the unit limit stay, since both are lobby settings the host picks for everyone.
- The computer's attack waves now travel as a crowd too. A selection you send, and a computer team told to gather at a point, were already handed a line across the road. The order that actually sends a wave down an approach path was not: every tank on it steered at the middle of the same road, which is the single file the crowd was written to stop. That order now hands out the same lines a move does, aimed at the next point on the path so a group already standing on the first waypoint still has a direction to spread across.
- Since the first attempt at the band failed silently and passed every test it had, the band can now be drawn. A switch puts each moving unit's route on the ground in blue, the line it holds as a yellow mark sideways from the unit, and the offset that survived as a green mark from the route out to where the unit is actually aiming. The three failures that look identical from the camera - a line never given out, a line given out and dropped on the way, a line applied but too small to see - are three different pictures under it, and the same run writes them to the log as well.

---

## A new map whenever you want one

- The skirmish map list ends with three random maps, one per size. Pick one and the game builds it,
  then starts the match on it; pick it again and you get a different one. There is nothing to
  install and nothing to download, and the map shows up in the list with a picture like any other.
  The bigger the game, the bigger the map: every player who joins brings ground with them, so an
  eight-way fight is not eight bases crammed onto a duel map.
- No two of them are the same shape. Nothing is mirrored, rotated or laid out on a ring. The whole
  map is one field of noise, and the bases are found in it: the flattest ground first, then
  whichever good ground is furthest from everything already taken. Some seeds give you four corners
  and some give you a long diagonal, and where the fighting happens is different every time.
- Nobody starts with their back against the edge of the map. A seventh of the map is kept clear
  of start positions on every side: there is ground behind a base to fall back into, and a way
  round it for whoever is attacking. On a packed map the seats sit on a circle so the last
  players are not the ones walked into the corners.
- It is a map, not a field. The ground rolls the way Twilight Flame does: hills you can drive, a
  ridge that drops off, a hollow the water sits in. High ground is still worth holding, but it is
  a hill, not a table with a cliff off every side. Water follows the valleys: a lake in a hollow
  and a stream running down to it, not a round puddle stamped on the grass. Dirt covers the high
  ground, rock breaks through where it turns steep, and woods stand thick in the middle and thin at
  the edges. The ground changes texture the way ground does, with one blending into the next instead
  of meeting it at a straight line.
- The maps are twice the size they were. A two-player map is a quarter of a million square feet of
  ground, an eight-player one nearly three times that, which is room to manoeuvre round a flank
  instead of running into the enemy on the way out of your own base.
- There are towns on it, and no two of them are the same town. Each one is built out of the number
  the map came from: how many streets it has, how big its blocks are, how deep the plots run and
  which way the whole grid faces. Shops and apartments stand back from the kerb facing the road,
  with yards and corners left empty between them and the junctions kept clear. A town is somewhere
  to fight through, and the roads are real roads, so what drives down one drives faster.
- Streets stop at the water. A road paves whatever is under it, lake bed included, so a town on a
  bank used to run its high street into the lake; now the street ends at the shore and the town
  keeps its shape.
- Nothing stands on a slope. The ground under every building, every supply dock and every oil
  derrick is levelled before it goes down and eased back into the hillside around it, so you do not
  arrive at a dock sunk into a bank or a house hanging off a ledge.
- The bunkers moved out of the towns and onto the ramps. A bunker now sits where the route between
  two levels is, which is the ground worth holding, and it comes with room around it to fight over.
- The water has a shore, and it is in a hollow. The lake bed shelves away from the bank instead of
  dropping off it, so there is shallow water at the edge with the light coming through it, and you
  can hear it from the bank. The ground around a lake is above the water it holds, which sounds
  obvious and was not: lakes are cut into the lowest ground on the map, and the land further out
  used to lie lower than the water, so a lake read as a puddle sitting on top of the map.
- Everybody opens with the same money. The dock beside your base sits the same walk from home as
  everybody else's, on the same compass from the middle of the map. The one out in the field sits
  on your side of the line to whoever is nearest, not on the one flat cell the first seat used to
  take. Two oil derricks sit on your flanks and two small supply piles on the diagonals, so one
  player does not draw the lot in their yard. Things on the map face the compass: forty-five
  degrees, not a random spin.
- The number that made the map is in its name, so the same number always makes the same map, on
  every machine. Two people who type it get the same ground, and a map worth keeping can be found
  again.
- A generated map is never written to your disk. It is built when the match starts and lives in
  memory, and nothing is left behind afterwards - no map folder per roll, no preview files, no
  entry in the map cache. The name still says which map it is, so the replay of a generated match
  plays back on any machine: it rebuilds the same ground from the same name, and so does anybody
  who joins.
- A supply dock behind a cliff, and a start with one way out, were why the generated maps left
  the skirmish list. Those seeds do not come out any more. The rows stay off the list until we
  put them back; `-randommap` still names the seed, and `-randommaps` puts the rows in.

---

## Not there yet
- Online and LAN play are untested.
- A frame through Direct3D 11 still costs a little more than it does through the old renderer: 7.6ms against 6.3ms in a screen full of inferno cannon fire. `-d3d9` on the command line puts the old renderer back on its own.
- You need to own the game; no game data ships here.
