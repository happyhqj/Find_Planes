# Find Planes

This is a game similar to classic naval chess. It includes two modes: Single_player mode and multiplayer mode.

Single player mode: The system will randomly place $4$ planes in a grid size of $12\times 12$. As a player, you need to accurately shoot down the planes (i.e. find the nose) within a limited number of operations. Every time you click on a position, the map will display whether it is empty, the fuselage, or the nose. When you shoot down all the planes within the specified number of steps, you win; otherwise, you fail.

Online mode: Local area network connection is used here, where the host creates a port and the client joins the game by entering an IP address. Both sides will first place four planes in their own maps, and then take turns attacking each other. The first person to shoot down the four planes of the opponent wins (note that after the first person shoots down all the planes of the second person, the second person still has one chance to operate. If the second person happens to knock down the last plane of the first person in this game, the game will be tied).

## Design Choice 1: Single-player mode

I used to play this game offline with my classmates, so if there's no one around, I can't play it. Now, the emergence of single player mode allows you to play on your own anytime, anywhere, and constantly challenge your best performance

## Design Choice 2: Taggable and Removable Tagging Features

In offline gameplay, you can use a pencil for reasoning. In this program, you can also add a transparent blue mark by right clicking the mouse. More importantly, this mark can be removed by clicking again, which is not easy to achieve in reality (after all, erasing it with an eraser is more troublesome than right clicking)

## Design Choice 3: The unique shape of airplanes

Compared to traditional naval chess ($1\times 1...1\times 4$), airplanes have more complex modeling and significantly higher reasoning difficulty during gameplay, making them more challenging. Players can confuse their opponents by embedding the tail and wings of the aircraft, greatly increasing the fun

## How to run this program?

You just need to put the file in any place and write these commands.

#### Build

```
cd /path/to/your/project
g++ -std=c++17 -Wall -g main.cpp -o main \
  -I/opt/homebrew/include \
  -L/opt/homebrew/lib \
  -lsfml-graphics -lsfml-window -lsfml-system -lsfml-network

```

#### Run

```
./main
```