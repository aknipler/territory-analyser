# Dev Notes

## Edge detection methods

Worth optimising?

## Territory representation

Re the Intelligent Territory Extension section: Would it be better to use a Gaussian Clustering Algorithm? Might make it easier to solve for extending territory.

## Overlapping / contested territory 

### Finding the territory
- When a building is placed, could check every player to see if there is overlap.
(8 player worst case) In building placement add O(7(r+4)^2) where r is the radius of the building 
- When a new truth board is made, compare the truthboards to find overlap 
(8 player worst case) After truth board creation add O(7n^2) where n is the size of the board. 
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

## Intelligent territory extension.

Simple example: If one player builds a wall across the center half of the map, and there is no opposition on their side of the map, then that player's territory should "fill" that entire half of the map, up to their walls.

Harder example: Can you still make this true, if the enemy snuck a few small buildings into the back of the player's base? 

Can we include forests in the making of walls/territory? How will we know if it's been chopped down?
