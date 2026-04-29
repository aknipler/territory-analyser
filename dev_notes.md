# Dev Notes

## To - Do
- The current wall ownership theory needs work -> lots of potential issues (if an enemy house is on the inside of the walls and is touching the wall, then its obstruction will be included and the boost will fail). Should do this more rigorously: After removing of dilations, walk the players obstructions + gaps that neighbour the current fill. If it is unbroken (cyclic with itself or touching two distinct map edges) then it's good.  

~ Need to improve hasCycle in conjunction with box checks. hasCycle could be proven in a bunch of ways the don't actually encircle the region of interest, it could cycle a simple circle of 4 cells (like a box).   
~ We currently do this by cell, I wonder if it would be better to do it by building object. (faster? More rigorous?)
* Some more details:  
**Issue A:** we only add player obstruction cells adjacent to the fill. If an opposing players obstruction is on the inside of the shape, touching the edge, then there will be a gap and the enclosed shape will fail to be recognised.  
**Solution A:** For these obstructions that are owned by another player or gaia, search their 4 adjacent neighbours recursively. If it finds a current-player cell, add it to the closure map, then kill that branch. Also, when you enter a call, if that cell neighbours a non-obstruction cell then don't explore it's neighbours.   
**Issue B:** If a player has a long thick wall that covers half the map and is connected to this subsection of the map, it would traverse the long wall even if half the fill was surrounded by an opposing player.  
**Solution B:** Make sure you get the whole connecting wall, in case it touches the edge of the map in 3 places or 4 places. Then, after you've got all the information, do a subsection analysis by coonnecting the map-edge-meeting points through the outside of the map. If there is another fill inside this shape / subsection, then it's an incorrect border for the fill.  

- Optimise
- Make it so contested territory method flash is able to be combined with the other methods.

## Thoughts 14/04/2026

Step 3 of the Fill algorithm. It's a little crazy right now, I'm just trying to get it behaving the way I want it to. It feels like I repeat a lot of steps,
there's a lot of abstraction and potential simplificatoin that can be done. First, we currently visit all dilation cells, then later on remove them depending on what 
they're next to. Maybe we can do an initial remove of dilation cells that are only next to obstructions, fill and other dilation cells. 
If we repeated this with the next fill cycle and only did the gap check at the end, all the remaining dilation cells at the end of all the fills would either be islands or gaps.

Should also improve it to get the outline of the shape, then check what the filling is. That way if there is
a small enemy base inside the fill, it won't immediately disqualify the whole fill.
-> could be fixable by using dead-end theory to not consider obstructions that are dead ends (is it correct that dead-ends will always be internal? I believe so)

## Edge detection methods

Worth optimising?

### Four-box:
- checks one box above, one box to the side and one box diagonally. If all four boxes are not the same value, then it's an edge.

## Territory representation

Re the Intelligent Territory Extension section: Would it be better to use a Gaussian Clustering Algorithm? Might make it easier to solve for extending territory.

## Overlapping / contested territory 

### Finding the territory
DID NOT IMPLEMENT:
- When a building is placed, could check every player to see if there is overlap.
(8 player worst case) In building placement add O(7(r+4)^2) where r is the radius of the building 
- When a new truth board is made, compare the truthboards to find overlap 
(8 player worst case) After truth board creation add O(7n^2) where n is the size of the board. 
IMPLEMENTED: 
- Instead of one bool board for each player, combine all bool boards into one size_t board. Each player has a number associated with them: 1,2,4,8,16,32,64,128. On the board, you add players numbers together, so you can see which teams/players are overlapping just by looking at the numbers e.g. if the number is 12 then it has to be 8 + 4, so player 4 and player 3.  
Logistics: Track changes of a players bool board. Add those values to the global size_t board. Run the finalise shading function to turn board into color values in a 2D array at desired resolution.
(8 player worst case) Have to run finalise shading function anyway. The main logistic change would be 'tracking changes' but that's more of a memory performance change. I think this is best.

#### Two team solutions
What solves this behaviour? Could do  
a) represent it with both colours, with neutral lines to show its contested  
b) give each team half the territory, whichever half is closest to them. (could be useful if you have 2 teams of 4 but put it in individual player viewability, so that where teammates are, the colours will just snug with each other.)  

Maybe both behaviours will be necessary

#### Multi-team solutions 
i.e. what if 3 teams all have overlapping territory in the same spot?
I think in this case, just have a generic "contested" shading option. 
I like: 
- Give each player/team part of the contested territory, but make it slightly more opaque and make it flash to white and back again.

## Intelligent territory extension.

Simple example: If one player builds a wall across the center half of the map, and there is no opposition on their side of the map, then that player's territory should "fill" that entire half of the map, up to their walls.

Harder example: Can you still make this true, if the enemy snuck a few small buildings into the back of the player's base? 
Answer: Yes.
- Compare percentage ownership for the fill. If player has >85%? total points then they get the fill. 

Can we include forests in the making of walls/territory? How will we know if it's been chopped down?
- Trees are just objects. Have a Gaia grid, keep track of it.
