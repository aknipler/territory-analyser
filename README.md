# Territory Analyser

Welcome to territory analyser. This is a work-in-progress. If you would like to integrate this into your own software, please message me.

This project was designed with Age of Empires 2 in mind, however it has been intentionally designed to be easily adaptable to any RTS and some board games. 

## Setup 

1. The project uses OpenCV to turn the output arrays into color mats. You can install OpenCV by following the instructions here: https://docs.opencv.org/4.x/df/d65/tutorial_table_of_content_introduction.html

2. Build buildings_dict. See "How it Works" for a detailed explanation.

3. Build the nonWalkableTerrainBoard (fix the temporary function findNonWalkableTerrainEdges()). This is a board that marks all the terrain that can't be walked on (i.e. water). This is different to objects that can be destroyed (i.e. buildings, trees, limited gold mines).

4. Edit the settings.json file to match your configuration. If integrating into a game or spectating application, you can skip this step and define your config using the AppConfig namespace.

5. Have fun!



## Common Errors

#### -Ensure that GCC and G++ are 17+ compatible-
If on Linux, run `sudo apt update && sudo apt upgrade gcc g++`, and check the version with `gcc -v`.

If the version is still incorrect, it may be due to symlink issues from multiple gcc versions on the system.

#### -If using VS Code-
- You will need to update C/C++ properties JSON file. Open Command Palette and search for C/C++: Edit Configurations (JSON). Add ` "/usr/local/include/opencv4",`
- You may need to go into Preferences > Settings, Search "cppstandard" and set to c++17.
- In .vscode/tasks.json add the following lines to the cppbuild args section:

```
    "-std=c++17",
    "${workspaceFolder}/*.cpp",
    // -o
    "-I/usr/local/include/opencv4", // Include path
    "-L/usr/local/lib", // Library path
    "-lopencv_imgcodecs", // Example library flags
    "-lopencv_core",
    "-lpthread",
    "-lopencv_highgui",
    "-lopencv_imgproc"
```

- It may also be helpful to add this section above the curly braces of the cpp build:

```
    {
        "label": "build",
        "type": "shell",
        "command": "${command:cmake.buildCurrentTarget}",
        "group": {
            "kind": "build",
            "isDefault": true
        },
        "options": {
            "cwd": "${workspaceFolder}/build"
        },
        "problemMatcher": [
            "$gcc"
        ]
    },
```

- Use CMakeBuild and run (bottom left corner of VS Code, not the top right corner).


## Usage

### Workflow

There is a simple 4 step process:

1. Initialise your system (map size, players, teams, neutral objects).
2. Add player-controlled objects (i.e. buildings) to the grids using analyser.updateBuilding(x,y,building,player,type) // type = "add" or "remove"
3. When you want a visual update use updateRender() which will perform fill analysis etc. and update the output mat.
4. Use getFinalTerritoryMap("player" or "team") to get the output mat. Display the output mat using imshow(), imwrite() or integrate into your game/spectating application.

- Perform steps 2 - 4 again as needed.
- If using "flash" contested territory method, perform updateContestedFlash() every frame.


## How It Works

### Raw Territory 

Buildings add influence around them, as per attributed defined in buildings_dict. 
The influence is stored on a board with cumulative weightings. 
For example, a castle may give 7 score to every tile within 8 radius of it, with a soft edge of 5. A house may give 3 to a radius of 0 with a soft edge of 1. 

Then we define a threshold that a cell needs to surpass to be owned by a player i.e. a threshold of 3. 
We store the mappings of what each player owns. So there are two mappings, a mapping for weights and a mapping for defined ownership from raw territory. 

The MasterBoard of player ownership uses additive binary layering 
i.e. if player 1 owns a cell, it has a score of 1, player 2 has a score of 2, player 3 has a score of 4. 
If player 1 and 2 contest a cell, then it has a score of 3.

**Soft Edges:**  
If a house is 9 tiles away from a castle, their radii won't overlap but their soft edges will. 
In this way, the fact that the buildings are close to each other provides extra territory (intuitive) and looks more visually appealing.


### Contested Territory

There are several controls for how to deal with contested regions. They are  
- "growth": assign contested cell ownership to players based on distance to nearest obstructions  
- "flash": make the contested area flash (changes the alpha output)  
- "staticColour": set it to one colour (i.e. grey)  

The method can be set in the config file.

### Edges of territory

Edges are found by a box check on every cell. The box check looks at (i,j), (i+1,j), (i,j+1) and (i+1,j+1). If any of these 4 cells have different ownership, then (i,j) is an edge. 

### Fill

Fill uses obstructions to make territory closures. It takes a fill-first approach. The steps are:
1. Merges all obstruction boards (including Gaia at key 0) into one wall map.  
2. Dilates those walls (and the edge of the map) by one cell so that nearly closed shapes can be treated as closed. 
3. Runs flood fill over all remaining open cells to find candidate regions.
4. Detects gaps around each fill using dilated walls.
5. Explores the obstructions around the fill to determine if it is closed by one player.
6. Each final region is assigned to a dominant player based on local territory presence in that region, with an extra boost when one player is the sole owner of bordering obstruction cells (i.e. when a player has completely walled off a section of the map). 
7. The output of the fill process is a fill board and the gap information.

### Color pass

The color pass takes all the information in the system (neutral objects, edges of territory, territory and fill-in closed-shape territory) and layers them into a final output. The priority is:

1. Neutral objects on top (so no territory analysis affects the colour or visibility of neutral objects)
2. Edges 
3. Raw Territory (from the influence of buildings)
4. Fill is last (filling closed or nearly closed shapes from obstructions)

This method deals simply with an enemy's object being shown when inside of a filled shape.

### Buildings_dict

Buildings are an essential part of any game. They represent obstructions on the map that can't be traversed. They are also the pieces that define territory. Hence, we need a dictionary of all the buildings and what their properties are. The included buildings dict contains examples, however implementing your own comprehensive buildings_dict could be time consuming. Here are some suggestions to speed up the process:
- Find attributes that can help you set some standard logic. I.e. if it is a ranged building like a castle, krepost, tower, fortified church, dock, relate the territory influence to the range of the object.
- Set a standard for buildings that have no ranged attack but can build military units (i.e. barracks, archery range, stable).
- Set another standard for buildings that have no range and can't build military units (i.e. walls, houses, market, university).
- Consider exceptions that you might want to handle separately such as siege workshops, monasteries and outposts.

Attributes in buildings_dict:
- Radius (size of territory influence the building has on it's surroundings)
- Weighting (strength of influence)
- Soft edge (how quickly influence decreases after radius distance)
- Soft edge starting value division factor (soft_edge linearly decreases from THRESHOLD/SoftEdgeDivFactor to 0)
- Width (width of building)
- Height (height of building)

In buildings_dict you will also need to build the list of ranged_buildings.