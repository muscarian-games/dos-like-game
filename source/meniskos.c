// Game: Trials of Meniskos, adapted from:
// Port of raycast tutorial code by Lode Vandevenne
// https://lodev.org/cgtutor/raycasting.html
// See below for license.
//
//    /Mattias Gustavsson

/*
Copyright (c) 2004-2020, Lode Vandevenne

All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <math.h>
#include "dos.h"

#define screenWidth 320
#define screenHeight 200
#define texWidth 16 // must be power of two
#define texHeight 16 // must be power of two
#define mapWidth 10
#define mapHeight 10

// TODO: Generate worldmaps per floor
// TODO: Avoid using floor/ceiling textures for walls
// Zero (0) is the floor, and the rest are walls.
// Texture for walls decided by subtracting 1 from the value.
// 10 x 10 mazelike grid:
int worldMap[mapWidth][mapHeight] =
{
  {3,3,3,3,3,3,3,3,3,3},
  {3,0,0,0,2,0,1,1,1,3},
  {3,0,0,0,0,0,0,0,0,3},
  {3,1,0,1,1,1,1,1,1,3},
  {3,1,0,0,0,1,0,0,0,3},
  {3,1,0,1,0,1,0,1,1,3},
  {3,1,0,1,0,1,0,0,1,3},
  {3,1,0,1,0,1,1,0,1,3},
  {3,1,0,0,0,0,0,0,1,3},
  {3,3,3,3,3,3,3,3,3,3}
};

typedef enum GameStateType {
  MENU,
  PLAYING,
  PAUSED,
  GAMEOVER,
  WIN
} GameStateType;

typedef enum PlayerStateType {
  PLAYER_NORMAL, // moving or standing still
  PLAYER_ATTACKING, // cannot move or block while attacking
  PLAYER_BLOCKING, // moves slowly while blocking, attack interrupts block
} PlayerStateType;

typedef struct GameState
{
  GameStateType state;
  PlayerStateType playerstate;
  int level;
  // Player stats
  int score;
  int health;
  int stamina;
  // Player position
  double posX;
  double posY;
  double posZ; // Unused unless we add crouch/jump back.
  double dirX;
  double dirY;
  double planeX;
  double planeY;
  double pitch;
} GameState;

const GameStateType startingState = MENU;
GameState state = { startingState, PLAYER_NORMAL, 0, 0, 100, 100 };

typedef struct Sprite
{
  double x;
  double y; // is y=0 top left or?
  int texture;
  int frames;
  int id;
} Sprite;

#define numSprites 1

Sprite sprite[numSprites] =
{
  {5.5, 6.5, 6, 2, 1} // Worm enemy
};

typedef struct Weapon {
  int texture;
  int attackTexture;
  int damage;
  int range;
  int cooldown;
  int animCooldown; // consistently like 24 frames?
  int attackSpeed;
} Weapon;

int weaponAnimCooldown = 24; // frames

Weapon weapon[1] = {
  { 8, 9, 10, 1, 0, 2 } // sword
};

typedef enum EnemyStateType {
  IDLE,
  MOVING,
  ATTACKING,
  DEAD
} EnemyStateType;

typedef struct Enemy
{
  int health;
  int damage;
  EnemyStateType state;
  double movementRange; // moves to player when player in range, in tiles
  double movementSpeed; // how many frames to move 1 pixel //TODO: Compare to how player speed is handled
  double attackRange; // attacks player when player in range, in tiles
  int attackCooldown; // how long in frames between attacks
  int attackSpeed; // how long in frames between attack telegraph (frame 2 of gif) and actual attack
  int spriteId; // used to grab this enemy's struct when iterating sprites, so the relationship is Sprite->Enemy
  int cooldown; // how long in frames until next action
  int normalTexture;
  int attackTexture;
  int deadTexture;
} Enemy;

const int numEnemies = 1;

Enemy enemies[1] = {
  { 100, 10, IDLE, 12.0, 12.0, 1.0, 3, 2, 1, 0, 6, 7, 6 }
};

//1D Zbuffer
double ZBuffer[screenWidth];

//arrays used to sort the sprites
int spriteOrder[numSprites];
double spriteDistance[numSprites];

//function used to sort the sprites
void sortSprites(int* order, double* dist, int amount);

double dmax( double a, double b ) { return a > b ? a : b; }
double dmin( double a, double b ) { return a < b ? a : b; }

// constant indexes for textures
int floor1 = 3;
int floor2 = 4;
int ceilingTexture = 5;
int numTextures = 9;

void set_textures(uint8_t* texture[numTextures], int tw, int th, int palcount, uint8_t palette[768])
{
  texture[0] = loadgif( "files/meniskos/brick-wall-mono.gif", &tw, &th, &palcount, palette );
  texture[1] = loadgif( "files/meniskos/wood-wall-mono.gif", &tw, &th, &palcount, palette );
  texture[2] = loadgif( "files/meniskos/brick-wall-pillar-mono.gif", &tw, &th, &palcount, palette );
  texture[3] = loadgif( "files/meniskos/brick-floor-mono.gif", &tw, &th, &palcount, palette ); // floor 1
  texture[4] = loadgif( "files/meniskos/brick-floor-mono-2.gif", &tw, &th, &palcount, palette ); // floor 2
  texture[5] = loadgif( "files/meniskos/brick-ceiling-mono.gif", &tw, &th, &palcount, palette ); // ceiling

  // load some mobile sprite textures
  texture[6] = loadgif( "files/meniskos/worm_01.gif", &tw, &th, &palcount, palette );
  texture[7] = loadgif( "files/meniskos/worm_02.gif", &tw, &th, &palcount, palette );
  texture[8] = loadgif( "files/meniskos/sword.gif", &tw, &th, &palcount, palette );
  texture[9] = loadgif( "files/meniskos/sword_hit.gif", &tw, &th, &palcount, palette );
}

//TODO: associate tracks with levels?
int numTracks = 2;
void load_music(struct music_t* music[numTracks]) {
  setsoundbank( DEFAULT_SOUNDBANK_SB16 );
  music[0] = loadmid( "files/sound/meniskos_1.mid" ); // menu
  music[1] = loadmid( "files/sound/meniskos_2c.mid" ); // dungeon
}

void set_positions() {
  // posX = 4.0, posY = 2.5; // x and y start position, starting from (???) -- i think x,y is top left, let us start player in top right always to prevent weird look dir
  // dirX = -1.0, dirY = 0.0; // initial direction vector -- this can be messed with to fuck up player perspective but doesn't really change just the look dir... :|
  // planeX = 0.0, planeY = 0.66; //the 2d raycaster version of camera plane
  // pitch = 0; // looking up/down, expressed in screen pixels the horizon shifts
  // posZ = 0; // vertical camera strafing up/down, for jumping/crouching. 0 means standard height. Expressed in screen pixels a wall at distance 1 shifts
  // Rewrite the above but use the game state positioning:
  state.posX = 4.0;
  state.posY = 2.5;
  state.dirX = -1.0;
  state.dirY = 0.0;
  state.planeX = 0.0;
  state.planeY = 0.66;
  state.pitch = 0;
  state.posZ = 0;

}

int main(int argc, char* argv[])
{
  (void) argc, (void) argv;
  //TODO: Allow position to be set from level data
  set_positions();

  uint8_t* texture[numTextures];

  setvideomode( videomode_320x200 );
  int w = 320;
  int h = 200;
  setdoublebuffer(1);

  // load some textures
  int tw, th, palcount;
  uint8_t palette[ 768 ];

  set_textures(texture, tw, th, palcount, palette);
  struct music_t* music[numTracks];
  load_music(music);
  playmusic( music[1], 1, 255 );

  for( int i = 0; i < palcount; ++i ) {
      setpal(i, palette[ 3 * i + 0 ], palette[ 3 * i + 1 ], palette[ 3 * i + 2 ] );
  }

  uint8_t* buffer = screenbuffer();

  // start the main loop
  while(!shuttingdown())
  {
    waitvbl();
    // uses FLOOR CASTING algo
    for(int y = 0; y < screenHeight; ++y)
    {
      // whether this section is floor or ceiling
      bool is_floor = y > screenHeight / 2 + state.pitch;

      // rayDir for leftmost ray (x = 0) and rightmost ray (x = w)
      float rayDirX0 = (float)(state.dirX - state.planeX);
      float rayDirY0 = (float)(state.dirY - state.planeY);
      float rayDirX1 = (float)(state.dirX + state.planeX);
      float rayDirY1 = (float)(state.dirY + state.planeY);

      // Current y position compared to the center of the screen (the horizon)
      int p = (int)( is_floor ? (y - screenHeight / 2 - state.pitch) : (screenHeight / 2 - y + state.pitch) );

      // Vertical position of the camera.
      // NOTE: with 0.5, it's exactly in the center between floor and ceiling,
      // matching also how the walls are being raycasted. For different values
      // than 0.5, a separate loop must be done for ceiling and floor since
      // they're no longer symmetrical.
      float camZ = (float)( is_floor ? (0.5 * screenHeight + state.posZ) : (0.5 * screenHeight - state.posZ) );

      // Horizontal distance from the camera to the floor for the current row.
      // 0.5 is the z position exactly in the middle between floor and ceiling.
      // NOTE: this is affine texture mapping, which is not perspective correct
      // except for perfectly horizontal and vertical surfaces like the floor.
      // NOTE: this formula is explained as follows: The camera ray goes through
      // the following two points: the camera itself, which is at a certain
      // height (posZ), and a point in front of the camera (through an imagined
      // vertical plane containing the screen pixels) with horizontal distance
      // 1 from the camera, and vertical position p lower than posZ (posZ - p). When going
      // through that point, the line has vertically traveled by p units and
      // horizontally by 1 unit. To hit the floor, it instead needs to travel by
      // posZ units. It will travel the same ratio horizontally. The ratio was
      // 1 / p for going through the camera plane, so to go posZ times farther
      // to reach the floor, we get that the total horizontal distance is posZ / p.
      float rowDistance = camZ / p;

      // calculate the real world step vector we have to add for each x (parallel to camera plane)
      // adding step by step avoids multiplications with a weight in the inner loop
      float floorStepX = rowDistance * (rayDirX1 - rayDirX0) / screenWidth;
      float floorStepY = rowDistance * (rayDirY1 - rayDirY0) / screenWidth;

      // real world coordinates of the leftmost column. This will be updated as we step to the right.
      float floorX = (float)( state.posX + rowDistance * rayDirX0 );
      float floorY = (float)( state.posY + rowDistance * rayDirY0 );

      for(int x = 0; x < screenWidth; ++x)
      {
        // the cell coord is simply got from the integer parts of floorX and floorY
        int cellX = (int)(floorX);
        int cellY = (int)(floorY);

        // get the texture coordinate from the fractional part
        int tx = (int)(texWidth * (floorX - cellX)) & (texWidth - 1);
        int ty = (int)(texHeight * (floorY - cellY)) & (texHeight - 1);

        floorX += floorStepX;
        floorY += floorStepY;

        // choose texture and draw the pixel
        int checkerBoardPattern = ((int)(cellX + cellY)) & 1;
        int floorTexture;
        if (checkerBoardPattern == 0) floorTexture = floor1;
        else floorTexture = floor2;
        uint32_t color;

        if(is_floor) {
          // floor - get pixel
          color = texture[floorTexture][texWidth * ty + tx];
          buffer[ x + w * y ] = (uint8_t)color;
        } else {
          // ceiling - get pixel
          color = texture[ceilingTexture][texWidth * ty + tx];
          buffer[ x + w * y ] = (uint8_t)color;
        }
      }
    }

    // now WALL CASTING algo
    for(int x = 0; x < w; x++)
    {
      // calculate ray position and direction
      double cameraX = 2 * x / (double)(w) - 1; // x-coordinate in camera space
      double rayDirX = state.dirX + state.planeX * cameraX;
      double rayDirY = state.dirY + state.planeY * cameraX;

      // which box of the map we're in
      int mapX = (int)(state.posX);
      int mapY = (int)(state.posY);

      // length of ray from current position to next x or y-side
      double sideDistX;
      double sideDistY;

      // length of ray from one x or y-side to next x or y-side
      double deltaDistX = (rayDirX == 0) ? 1e30 : fabs(1 / rayDirX);
      double deltaDistY = (rayDirY == 0) ? 1e30 : fabs(1 / rayDirY);
      double perpWallDist;

      // what direction to step in x or y-direction (either +1 or -1)
      int stepX;
      int stepY;

      int hit = 0; // was there a wall hit?
      int side = 0; // was a NS or a EW wall hit?

      // calculate step and initial sideDist
      if(rayDirX < 0)
      {
        stepX = -1;
        sideDistX = (state.posX - mapX) * deltaDistX;
      }
      else
      {
        stepX = 1;
        sideDistX = (mapX + 1.0 - state.posX) * deltaDistX;
      }
      if(rayDirY < 0)
      {
        stepY = -1;
        sideDistY = (state.posY - mapY) * deltaDistY;
      }
      else
      {
        stepY = 1;
        sideDistY = (mapY + 1.0 - state.posY) * deltaDistY;
      }
      //perform DDA
      while (hit == 0)
      {
        // jump to next map square, either in x-direction, or in y-direction
        if(sideDistX < sideDistY)
        {
          sideDistX += deltaDistX;
          mapX += stepX;
          side = 0;
        }
        else
        {
          sideDistY += deltaDistY;
          mapY += stepY;
          side = 1;
        }
        // Check if ray has hit a wall
        if(worldMap[mapX][mapY] > 0) hit = 1;
      }

      // Calculate distance of perpendicular ray (Euclidean distance would give fisheye effect!)
      if(side == 0) perpWallDist = (sideDistX - deltaDistX);
      else          perpWallDist = (sideDistY - deltaDistY);

      // Calculate height of line to draw on screen
      int lineHeight = (int)(h / perpWallDist);

      // calculate lowest and highest pixel to fill in current stripe
      int drawStart = (int)( -lineHeight / 2 + h / 2 + state.pitch + (state.posZ / perpWallDist) );
      if(drawStart < 0) drawStart = 0;
      int drawEnd = (int)( lineHeight / 2 + h / 2 + state.pitch + (state.posZ / perpWallDist) );
      if(drawEnd >= h) drawEnd = h;
      //texturing calculations
      int texNum = worldMap[mapX][mapY] - 1; //1 subtracted from it so that texture 0 can be used!

      // calculate value of wallX
      double wallX; //where exactly the wall was hit
      if(side == 0) wallX = state.posY + perpWallDist * rayDirY;
      else          wallX = state.posX + perpWallDist * rayDirX;
      wallX -= floor((wallX));

      //x coordinate on the texture
      int texX = (int)(wallX * (double)(texWidth));
      if(side == 0 && rayDirX > 0) texX = texWidth - texX - 1;
      if(side == 1 && rayDirY < 0) texX = texWidth - texX - 1;

      // TODO: an integer-only bresenham or DDA like algorithm could make the texture coordinate stepping faster
      // How much to increase the texture coordinate per screen pixel
      double step = 1.0 * texHeight / lineHeight;
      // Starting texture coordinate
      double texPos = (drawStart - state.pitch - (state.posZ / perpWallDist) - h / 2 + lineHeight / 2) * step;
      for(int y = drawStart; y < drawEnd; y++)
      {
        // Cast the texture coordinate to integer, and mask with (texHeight - 1) in case of overflow
        int texY = (int)texPos & (texHeight - 1);
        texPos += step;
        uint32_t color = texture[texNum][texHeight * texY + texX];
        //make color darker for y-sides: R, G and B byte each divided through two with a "shift" and an "and"
        if(side == 1) color = (color >> 1) & 8355711;
        buffer[ x + w * y ] = (uint8_t)color;
      }

      //SET THE ZBUFFER FOR THE SPRITE CASTING
      ZBuffer[x] = perpWallDist; //perpendicular distance is used
    }

    // HANDLE ENEMY MOVEMENT/ATTACK
    for(int i = 0; i < numEnemies; i++) {
      // Get enemy data:
      Enemy enemy = enemies[i];

      // Check if enemy is dead:
      if (enemy.state == DEAD) continue;

      if (enemy.state != DEAD && enemy.health <= 0) {
        enemy.state = DEAD;
        enemies[i] = enemy;
        // TODO: set enemy sprite to dead sprite
        continue;
      }

      // Ensure we have grabbed the correct sprite:
      int spriteId = enemy.spriteId;
      int spriteIndex = 0;
      Sprite enemySprite = sprite[spriteIndex];
      if (enemySprite.id != spriteId) {
        for (int j = 0; j < numSprites; j++) {
          if (sprite[j].id == spriteId) {
            enemySprite = sprite[j];
            spriteIndex = j;
            break;
          }
        }
      }

      // Handle moving to and attacking player character:
      double distanceToPlayer = sqrt((state.posX - enemySprite.x) * (state.posX - enemySprite.x) + (state.posY - enemySprite.y) * (state.posY - enemySprite.y));
      printf("Distance to player: %f\n", distanceToPlayer);
      printf("Enemy state: %d\n", enemy.state);
      // Enemy is in a state where they can start/continue moving:
      if (enemy.state == IDLE || enemy.state == MOVING) {
        // Enemy sprites move to player if player within movementRange but not within attackRange:
        if (distanceToPlayer <= enemy.movementRange && distanceToPlayer > enemy.attackRange) {
          // Move enemy towards player
          //TODO: play "alert" sfx
          //TODO: Check for walls in the way, similar to player movement code?
          double moveDir = atan2(state.posY - enemySprite.y, state.posX - enemySprite.x);
          double speedPerFrame = enemy.movementSpeed / 60.0; // 60 fps
          double xMovement = cos(moveDir) * (speedPerFrame / (double)texWidth);
          double yMovement = sin(moveDir) * (speedPerFrame / (double)texWidth);
          printf("Enemy moving to player\n Enemy position: %f, %f\n Movedir %f\n xMovement %f\n yMovement %f\n", enemySprite.x, enemySprite.y, moveDir, xMovement, yMovement);
          enemySprite.x = enemySprite.x + xMovement;
          enemySprite.y = enemySprite.y + yMovement;
          enemy.state = MOVING;
          printf("After move enemy position: %f, %f\n", enemySprite.x, enemySprite.y);
        } else {
          enemy.state = IDLE;
          printf("Enemy idle\n");
        }

        if (enemy.state == IDLE && distanceToPlayer <= enemy.attackRange && enemy.cooldown <= 0) {
          // Enemy is in range to attack player, so start attack telegraph
          //TODO: play attack sfx
          enemy.state = ATTACKING;
          printf("Enemy attacking (telegraph)\n");
          enemySprite.texture = enemy.attackTexture;
          enemy.cooldown = enemy.attackSpeed * 60.0;
          //TODO: Sfx
        }
      }

      printf("Enemy cooldown is %d\n", enemy.cooldown);

      //TODO: Set enemy sprite to attack telegraph
      //TODO: Start telegraph cooldown
      //TODO: Check post-attack cooldown too?
      //TODO: State machine?
      if (enemy.state == ATTACKING) {
        if (distanceToPlayer <= enemy.attackRange && enemy.cooldown <= 0) {
          // Deal damage to player
          printf("Enemy attacking (deal damage)\n");
          //TODO: play hit sfx based on blocking vs. not
          enemy.cooldown = enemy.attackCooldown * 60.0;
          int totalDamage = state.playerstate == PLAYER_BLOCKING ? enemy.damage / 2 : enemy.damage;
          state.health -= totalDamage;
          printf("Player health: %d\n", state.health);
          enemy.state = IDLE;
          enemySprite.texture = enemy.normalTexture;
        } else if (distanceToPlayer > enemy.attackRange) {
          printf("Player out of attack range now, idling");
          // Enemy is out of attack range, so stop attack telegraph
          enemy.state = IDLE;
          enemySprite.texture = enemy.normalTexture;
        }
      }

      // Update enemy and their sprite, must be done after all state changes:
      if (enemy.cooldown > 0) enemy.cooldown -= 1;
      enemies[i] = enemy;
      sprite[spriteIndex] = enemySprite;
    }


    // SPRITE CASTING
    // sort sprites from far to close
    for(int i = 0; i < numSprites; i++)
    {
      spriteOrder[i] = i;
      spriteDistance[i] = ((state.posX - sprite[i].x) * (state.posX - sprite[i].x) + (state.posY - sprite[i].y) * (state.posY - sprite[i].y)); //sqrt not taken, unneeded
    }
    sortSprites(spriteOrder, spriteDistance, numSprites);

    //after sorting the sprites, do the projection and draw them
    for(int i = 0; i < numSprites; i++)
    {
      //translate sprite position to relative to camera
      double spriteX = sprite[spriteOrder[i]].x - state.posX;
      double spriteY = sprite[spriteOrder[i]].y - state.posY;

      //transform sprite with the inverse camera matrix
      // [ planeX   dirX ] -1                                       [ dirY      -dirX ]
      // [               ]       =  1/(planeX*dirY-dirX*planeY) *   [                 ]
      // [ planeY   dirY ]                                          [ -planeY  planeX ]

      double invDet = 1.0 / (state.planeX * state.dirY - state.dirX * state.planeY); //required for correct matrix multiplication

      double transformX = invDet * (state.dirY * spriteX - state.dirX * spriteY);
      double transformY = invDet * (-state.planeY * spriteX + state.planeX * spriteY); //this is actually the depth inside the screen, that what Z is in 3D, the distance of sprite to player, matching sqrt(spriteDistance[i])

      int spriteScreenX = (int)((w / 2) * (1 + transformX / transformY));

      // parameters for scaling and moving the sprites
      #define uDiv 1
      #define vDiv 1
      #define vMove 0.0
      int vMoveScreen = (int)( (int)(vMove / transformY) + state.pitch + state.posZ / transformY );

      // calculate height of the sprite on screen
      int spriteHeight = abs((int)(h / (transformY))) / vDiv; //using "transformY" instead of the real distance prevents fisheye
      // calculate lowest and highest pixel to fill in current stripe
      int drawStartY = -spriteHeight / 2 + h / 2 + vMoveScreen;
      if(drawStartY < 0) drawStartY = 0;
      int drawEndY = spriteHeight / 2 + h / 2 + vMoveScreen;
      if(drawEndY >= h) drawEndY = h - 1;

      // calculate width of the sprite
      int spriteWidth = abs( (int)(h / (transformY))) / uDiv;
      int drawStartX = -spriteWidth / 2 + spriteScreenX;
      if(drawStartX < 0) drawStartX = 0;
      int drawEndX = spriteWidth / 2 + spriteScreenX;
      if (drawEndX >= w) drawEndX = w - 1;

      // loop through every vertical stripe of the sprite on screen
      for(int stripe = drawStartX; stripe < drawEndX; stripe++)
      {
        int texX = (int)(256 * (stripe - (-spriteWidth / 2 + spriteScreenX)) * texWidth / spriteWidth) / 256;
        // the conditions in the if are:
        // 1) it's in front of camera plane so you don't see things behind you
        // 2) it's on the screen (left)
        // 3) it's on the screen (right)
        // 4) ZBuffer, with perpendicular distance
        if (transformY > 0 && stripe > 0 && stripe < w && transformY < ZBuffer[stripe])
        for (int y = drawStartY; y < drawEndY; y++) // for every pixel of the current stripe
        {
          int d = (y-vMoveScreen) * 256 - h * 128 + spriteHeight * 128; //256 and 128 factors to avoid floats
          int texY = ((d * texHeight) / spriteHeight) / 256;
          uint32_t color = texture[sprite[spriteOrder[i]].texture][texWidth * texY + texX]; // get current color from the texture
          if ((color & 0x00FFFFFF) != 0) buffer[ stripe + w * y ] = (uint8_t)color; // paint pixel if it isn't black, black is the invisible color
        }
      }
    }

    // Show weapon in bottom left
    int weaponTexture = state.playerstate == PLAYER_ATTACKING || state.playerstate == PLAYER_BLOCKING ? weapon[0].attackTexture : weapon[0].texture;
    int weaponScale = 8;
    for (int x = 0; x < texWidth * weaponScale; x++) {
      for (int y = 0; y < texHeight * weaponScale; y++) {
        uint32_t color = texture[weaponTexture][texWidth * (y / weaponScale) + (x / weaponScale)];
        if ((color & 0x00FFFFFF) != 0) buffer[ x + w * ((screenHeight - (texHeight * weaponScale)) + y) ] = (uint8_t)(7); // paint pixel colors, 0 = ???, 1 = green, 2 = light blue, 3 = (???), 4 = red, 5 = fuchsia, 6 = orange, 7 = white 
      }
    }


    // Show UI overlay
    static char healthString[32];
    snprintf(healthString, 12, "HEALTH: %d", state.health);
    centertextxy(12, 12, healthString, 100);

    buffer = swapbuffers();
    // No need to clear the screen here, since everything is overdrawn with floor and ceiling

    //timing for input and FPS counter
    double frameTime = 1.0 / 60.0; //frametime is the time this frame has taken, in seconds

    //speed modifiers
    double speedModifier = state.playerstate == PLAYER_BLOCKING ? 0.5 : 1.0;
    double moveSpeed = frameTime * 3.0 * speedModifier; //the constant value is in squares/order
    double rotSpeed = frameTime * 2.0 * speedModifier; //the constant value is in radians/order

    // move forward if no wall in front of you //TODO: use this for npcs too
    if (keystate(KEY_UP))
    {
      if(worldMap[(int)(state.posX + state.dirX * moveSpeed*5)][(int)(state.posY)] == false) state.posX += state.dirX * moveSpeed;
      if(worldMap[(int)(state.posX)][(int)(state.posY + state.dirY * moveSpeed*5)] == false) state.posY += state.dirY * moveSpeed;
    }
    // move backwards if no wall behind you
    if(keystate(KEY_DOWN))
    {
      if(worldMap[(int)(state.posX - state.dirX * moveSpeed)][(int)(state.posY)] == false) state.posX -= state.dirX * moveSpeed;
      if(worldMap[(int)(state.posX)][(int)(state.posY - state.dirY * moveSpeed)] == false) state.posY -= state.dirY * moveSpeed;
    }
    // rotate to the right
    if(keystate(KEY_RIGHT))
    {
      //both camera direction and camera plane must be rotated
      double oldDirX = state.dirX;
      state.dirX = state.dirX * cos(-rotSpeed) - state.dirY * sin(-rotSpeed);
      state.dirY = oldDirX * sin(-rotSpeed) + state.dirY * cos(-rotSpeed);
      double oldPlaneX = state.planeX;
      state.planeX = state.planeX * cos(-rotSpeed) - state.planeY * sin(-rotSpeed);
      state.planeY = oldPlaneX * sin(-rotSpeed) + state.planeY * cos(-rotSpeed);
    }
    // rotate to the left
    if(keystate(KEY_LEFT))
    {
      //both camera direction and camera plane must be rotated
      double oldDirX = state.dirX;
      state.dirX = state.dirX * cos(rotSpeed) - state.dirY * sin(rotSpeed);
      state.dirY = oldDirX * sin(rotSpeed) + state.dirY * cos(rotSpeed);
      double oldPlaneX = state.planeX;
      state.planeX = state.planeX * cos(rotSpeed) - state.planeY * sin(rotSpeed);
      state.planeY = oldPlaneX * sin(rotSpeed) + state.planeY * cos(rotSpeed);
    }

    // Handle cooldowns related to player-actions
    if (state.playerstate == PLAYER_ATTACKING) {
      if (weapon[0].animCooldown > 0) {
        weapon[0].animCooldown -= 1;
      } else {
        state.playerstate = PLAYER_NORMAL;
      }

      if (weapon[0].cooldown > 0) {
        weapon[0].cooldown -= 1;
      } 
    }

    // attack/block
    if(state.playerstate != PLAYER_BLOCKING && keystate(KEY_SPACE) && weapon[0].cooldown <= 0)
    {
      // attack
      state.playerstate = PLAYER_ATTACKING;

      // start attack animation cooldown
      weapon[0].animCooldown = weaponAnimCooldown;
      weapon[0].cooldown = weapon[0].attackSpeed * 60.0;

      // Deal damage to NPCs in front of player and in range:
      for(int i = 0; i < numEnemies; i++) {
        // Get enemy data:
        Enemy enemy = enemies[i];

        // Check if enemy is dead:
        if (enemy.state == DEAD) continue;

        // Ensure we have grabbed the correct sprite:
        int spriteId = enemy.spriteId;
        int spriteIndex = 0;
        Sprite enemySprite = sprite[spriteIndex];
        if (enemySprite.id != spriteId) {
          for (int j = 0; j < numSprites; j++) {
            if (sprite[j].id == spriteId) {
              enemySprite = sprite[j];
              spriteIndex = j;
              break;
            }
          }
        }

        // Check if enemy is in range:
        double distanceToPlayer = sqrt((state.posX - enemySprite.x) * (state.posX - enemySprite.x) + (state.posY - enemySprite.y) * (state.posY - enemySprite.y));
        if (distanceToPlayer <= weapon[0].range) {
          // Deal damage to enemy
          enemy.health -= weapon[0].damage;
          printf("Enemy health: %d\n", enemy.health);
          //TODO: play hit sfx
          
          if (enemy.health <= 0) {
            // Enemy is dead, so set state to dead and set sprite to dead sprite
            enemy.state = DEAD;
            sprite[spriteIndex].texture = enemy.deadTexture;
            //TODO: play death sfx
          }
        } else {
          printf("attack missed");
          //TODO: play whiff sfx
        }
      }

    } else if (state.playerstate != PLAYER_ATTACKING && keystate(KEY_LSHIFT))
    {
      // block -- only happens while key is held down
      state.playerstate = PLAYER_BLOCKING;
    } else {
      // not attacking or blocking
      state.playerstate = PLAYER_NORMAL;
    }

    if(keystate(KEY_ESCAPE)) break;
  }
  return 0;
}


struct sortspr_t {
    double dist;
    int order;
};


int cmpspr(const void * a, const void * b) {
  struct sortspr_t const* spra = (struct sortspr_t const*)a;
  struct sortspr_t const* sprb = (struct sortspr_t const*)b;
  if( spra->dist < sprb->dist ) return -1;
  else if( spra->dist > sprb->dist ) return 1;
  else return 0;
}


//sort the sprites based on distance
void sortSprites(int* order, double* dist, int amount)
{
  struct sortspr_t sprites[ 256 ];
  for(int i = 0; i < amount; i++) {
    sprites[i].dist = dist[i];
    sprites[i].order = order[i];
  }
  qsort( sprites, amount, sizeof( *sprites ), cmpspr );
  // restore in reverse order to go from farthest to nearest
  for(int i = 0; i < amount; i++) {
    dist[i] = sprites[amount - i - 1].dist;
    order[i] = sprites[amount - i - 1].order;
  }
}
