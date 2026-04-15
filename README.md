# Welcome to territory controller (for AoE2, or any RTS, board game or optimisational curious mind)

## Setup 

1. The project uses OpenCV to turn the output arrays into color mats. You can install OpenCV by following the instructions here: https://docs.opencv.org/4.x/df/d65/tutorial_table_of_content_introduction.html

2. Edit the settings.json file to match your configuration. If integrating into a game or spectating application, you can skip this step and define your config using the AppConfig namespace.

## Common Errors:

#### -Ensure that GCC and G++ are 17+ compatible-
If on Linux, run

`sudo apt update && sudo apt upgrade gcc g++`

Check the version with

`gcc -v`

If the version is still incorrect, it may be due to symlink issues from multiple gcc versions on the system.

#### -If using VS Code-
- You will need to update C/C++ properties JSON file. Open Command Palette and search for C/C++: Edit Configurations (JSON). Add 

` "/usr/local/include/opencv4",`

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

3. Use CMakeBuild and run (bottom left corner of VS Code, not the top right corner).


## Usage

### Workflow

There is a simple 3 step process:

1. Initialise your system (map size, players, teams, neutral objects).
2. Add player-controlled objects (i.e. buildings) to the grids using analyser.updateBuilding(x,y,building,player,team,type) // type = "add" or "remove"
3. Perform the colour pass to render all the layers into a final image, std::tie(final_output, final_output_team) = analyser.colour_pass()
*. Perform steps 2 and 3 again as needed.

### Color pass

The color pass takes all the information in the system (neutral objects, edges of territory, territory and fill-in closed-shape territory) and layers them into a final output. The priority is:

1. Neutral objects on top
2. Edges 
3. Territory
4. Fill is last

This method deals simply with an enemy's object being shown when inside of a filled shape.


### Building buildings_dict

Note that soft_edge linearly decreases from the THRESHOLD to 0. 