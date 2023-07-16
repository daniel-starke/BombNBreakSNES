/**
* @file main.c
* @author Daniel Starke
* @copyright Copyright 2023 Daniel Starke
* @date 2023-07-03
* @version 2023-07-17
*
* Bomb'n'Break for SNES.
*/
#include <snes.h>
#include <stdint.h>
#include <string.h>
#include "debug.h"
#include "utility.h"
#ifdef HAS_BGM
#include "bgm1.h"
#endif /* HAS_BGM */


/**
 * Returns the number of elements in the given array.
 * Note that the array size needs to be known at compile time.
 *
 * @param x - array
 * @return number of elements in x
 */
#define ARRAY_SIZE(x) (sizeof(x) / sizeof(*(x)))


/** Default value for `maxTime` in seconds (needs to be a multiple of 10). */
#define DEF_MAX_TIME 180
/** Default value for `dropRate` in percent (needs to be a multiple of 5). */
#define DEF_DROP_RATE 35
/** Default value for `maxBombs` (at most `MAX_BOMBS`). */
#define DEF_MAX_BOMBS 5
/** Default value for `maxRange` (at most `MAX_RANGE`). */
#define DEF_MAX_RANGE 9
/** Maximum value for `maxBombs`. */
#define MAX_BOMBS 9
/** Maximum value for `maxRange`. */
#define MAX_RANGE 9


/** Time to live of a bomb in 1/10s units. */
#define BOMB_TTL 35
/** Time to live of the boots power-up in 1/10s units. */
#define BOOTS_TTL 150
/** May be set in `tPlayer.lastBombIdx` if the field is invalid. */
#define INVALID_LAST_BOMB_IDX 255
/** Time per bomb animation frame in 1/10s units. */
#define BOMB_ANIMATION 2
/** Time per player animation frame in 1/10s units. */
#define PLAYER_ANIMATION 1
/** Time per player animation frame in 1/10s units. */
#define EXPLOSION_ANIMATION 1


/** Default BGM volume (0..255). */
#define BGM_NORMAL_VOL 48
/** BGM volume when the game is paused (0..255). */
#define BGM_PAUSE_VOL 16


/** SNES background number for the foreground layer. */
#define FG_NR 0
/** SNES background number for the background layer. */
#define BG_NR 1
/** Optional value for bgSlideIn/bgSlideOut to ignore one SNES background parameter. */
#define INVALID_NR 7


/** SNES sprite ID for the player 1 sprite (in steps of 4). */
#define P1_NR 0
/** SNES sprite ID for the player 2 sprite (in steps of 4). */
#define P2_NR 4


/* player sprite boundary box (relative to the upper left corner) */
/** Defines the left extend of the player sprite. */
#define P_LEFT    4
/** Defines the right extend of the player sprite. */
#define P_RIGHT  12
/** Defines the upper extend of the player sprite. */
#define P_TOP     9
/** Defines the lower extend of the player sprite. */
#define P_BOTTOM 15
/** Defines the horizontal center of the player sprite. */
#define P_MID_X   7
/** Defines the vertical center of the player sprite. */
#define P_MID_Y  12


/** VRAM byte offset for `bg1Tiles`. */
#define CHR_VRAM_BG1 0x6000
/** VRAM byte offset for `fg1Tiles`. */
#define CHR_VRAM_FG1 0x8000
/** VRAM byte offset for `fg2Tiles`. */
#define CHR_VRAM_FG2 0xA000
/** VRAM byte offset for `p12Tiles`. */
#define CHR_VRAM_P1  0x0000
/** VRAM byte offset for background map page 1. */
#define MAP_VRAM_BG  0x2000
/** VRAM byte offset for foreground map page 1. */
#define MAP_VRAM_FG  0x4000


/** Size in bytes of a single 32x32 tile map. */
#define MAP_PAGE_SIZE (32*32*2)


/**
 * Converts the byte offset to a word offset.
 *
 * @param[in] x - byte offset to convert
 * @return word offset
 */
#define WORD_OFFSET(x) ((x) >> 1)


/**
 * Returns the VRAM address offset of the tile at
 * the given coordinate assuming a 32x32 screen.
 *
 * @param[in] x - x coordinate
 * @param[in] y - y coordinate
 * @return VRAM address offset (word offset)
 */
#define TILE_OFFSET(x, y) (((uint16_t)(y) * 32) + (x))


/**
 * Returns the VRAM address offset of the tile at
 * the given coordinate plus one assuming a 32x32 screen.
 *
 * @param[in] x - x coordinate
 * @param[in] y - y coordinate
 * @return VRAM address offset (word offset)
 */
#define TILE_OFFSET_1(x, y) (((uint16_t)(y) * 32) + (x) + 1)


/**
 * Returns the constructed tile attribute byte (high byte of a tile map entry).
 *
 * @param[in] y - flip vertial?
 * @param[in] x - flip horizontal?
 * @param[in] prio - priority?
 * @param[in] palette - palette number
 * @return tile map high byte (ignoring the upper tile index bits)
 */
#define TILE_ATTR(y, x, prio, palette) ((uint8_t)(((y) << 7) | ((x) << 6) | ((prio) << 5) | ((palette) << 2)))


/**
 * First element index in `fieldElemIndex` which is variable at game
 * initialization stage.
 */
#define FIRST_FLEX_FIELD 10


/** Vertical screen offset in pixels. Use -1 to render the first line correctly. */
#define VERT_OFFSET -1

/**
 *  @def SLIDE_SPEED
 *  Slide in/out speed in pixels per vertical blank.
 */
#ifdef USE_NTSC
#define SLIDE_SPEED 19
#else /* USE_PAL */
#define SLIDE_SPEED 24
#endif /* USE_PAL */


/**
 *  @def FP10HZ
 *  Frames (vertical blanks) per 1/10s.
 */
#ifdef USE_NTSC
#define FP10HZ 6
#else /* USE_PAL */
#define FP10HZ 5
#endif /* USE_PAL */


#if defined(HAS_BGM) || defined(HAS_SFX)
/**
 * Overwrite `WaitForVBlank()` to ensure that
 * spcProcess() is being called for every frame.
 */
#define WaitForVBlank() \
	spcProcess(); \
	WaitForVBlank()
#endif /* HAS_BGM or HAS_SFX */


/**
 * Delay execution a bit after each key pressed.
 */
#define clickDelay() WaitNVBlank(FP10HZ)


/**
 * Wait until the given key for the specified pad has been
 * released.
 *
 * @param[in] pad - use this pad
 * @param[in] key - wait for this key to be released
 */
#define waitForKeyReleased(pad, key) \
	while (padsCurrent((pad)) & (key)) { \
		WaitForVBlank(); \
	}


/**
 * Clears the tiles of a 16x16 game field element.
 *
 * @param field - upper left game field tile pointer
 */
#define clearField(field) \
	ASSERT_ARY_PTR(gameFieldLow, field + 0x00); \
	ASSERT_ARY_PTR(gameFieldLow, field + 0x01); \
	ASSERT_ARY_PTR(gameFieldLow, field + 0x20); \
	ASSERT_ARY_PTR(gameFieldLow, field + 0x21); \
	(field)[0x00] = FIELD_EMPTY; \
	(field)[0x01] = FIELD_EMPTY; \
	(field)[0x20] = FIELD_EMPTY; \
	(field)[0x21] = FIELD_EMPTY; \
	refreshGameScreenLow = true;


/**
 * Set the tiles of a 16x16 game field element.
 *
 * @param field - upper left game field tile pointer
 * @param offset - tile index base offset
 */
#define setField(field, offset) \
	ASSERT_ARY_PTR(gameFieldLow, field + 0x00); \
	ASSERT_ARY_PTR(gameFieldLow, field + 0x01); \
	ASSERT_ARY_PTR(gameFieldLow, field + 0x20); \
	ASSERT_ARY_PTR(gameFieldLow, field + 0x21); \
	(field)[0x00] = (uint8_t)((offset) + 0x00); \
	(field)[0x01] = (uint8_t)((offset) + 0x01); \
	(field)[0x20] = (uint8_t)((offset) + 0x10); \
	(field)[0x21] = (uint8_t)((offset) + 0x11); \
	refreshGameScreenLow = true;


/**
 * Set the tiles of a 16x16 game field element with both column interchanged.
 *
 * @param field - upper left game field tile pointer
 * @param offset - tile index base offset
 */
#define setFieldFlippedX(field, offset) \
	ASSERT_ARY_PTR(gameFieldLow, field + 0x00); \
	ASSERT_ARY_PTR(gameFieldLow, field + 0x01); \
	ASSERT_ARY_PTR(gameFieldLow, field + 0x20); \
	ASSERT_ARY_PTR(gameFieldLow, field + 0x21); \
	(field)[0x00] = (uint8_t)((offset) + 0x01); \
	(field)[0x01] = (uint8_t)((offset) + 0x00); \
	(field)[0x20] = (uint8_t)((offset) + 0x11); \
	(field)[0x21] = (uint8_t)((offset) + 0x10); \
	refreshGameScreenLow = true;


/**
 * Set the tiles of a 16x16 game field element with both rows interchanged.
 *
 * @param field - upper left game field tile pointer
 * @param offset - tile index base offset
 */
#define setFieldFlippedY(field, offset) \
	ASSERT_ARY_PTR(gameFieldLow, field + 0x00); \
	ASSERT_ARY_PTR(gameFieldLow, field + 0x01); \
	ASSERT_ARY_PTR(gameFieldLow, field + 0x20); \
	ASSERT_ARY_PTR(gameFieldLow, field + 0x21); \
	(field)[0x00] = (uint8_t)((offset) + 0x10); \
	(field)[0x01] = (uint8_t)((offset) + 0x11); \
	(field)[0x20] = (uint8_t)((offset) + 0x00); \
	(field)[0x21] = (uint8_t)((offset) + 0x01); \
	refreshGameScreenLow = true;


/**
 * Set the tiles of a 16x16 game field element to its next frame.
 *
 * @param field - upper left game field tile pointer
 */
#define nextFieldFrame(field) \
	ASSERT_ARY_PTR(gameFieldLow, field + 0x00); \
	ASSERT_ARY_PTR(gameFieldLow, field + 0x01); \
	ASSERT_ARY_PTR(gameFieldLow, field + 0x20); \
	ASSERT_ARY_PTR(gameFieldLow, field + 0x21); \
	(field)[0x00] = (uint8_t)((field)[0x00] + 2); \
	(field)[0x01] = (uint8_t)((field)[0x01] + 2); \
	(field)[0x20] = (uint8_t)((field)[0x20] + 2); \
	(field)[0x21] = (uint8_t)((field)[0x21] + 2); \
	refreshGameScreenLow = true;


/**
 * Set the tiles of a 16x16 game field attributes.
 *
 * @param field - upper left game field tile attribute pointer
 * @param attr - attribute value
 */
#define setFieldAttr(field, attr) \
	ASSERT_ARY_PTR(gameFieldHigh, field + 0x00); \
	ASSERT_ARY_PTR(gameFieldHigh, field + 0x01); \
	ASSERT_ARY_PTR(gameFieldHigh, field + 0x20); \
	ASSERT_ARY_PTR(gameFieldHigh, field + 0x21); \
	(field)[0x00] = (uint8_t)(attr); \
	(field)[0x01] = (uint8_t)(attr); \
	(field)[0x20] = (uint8_t)(attr); \
	(field)[0x21] = (uint8_t)(attr); \
	refreshGameScreenHigh = true;



/** Possible screens. */
enum {
	S_TITLE,
	S_OPTIONS,
	S_GAME,
	S_PAUSE,
	S_WINNER
};


/** Possible options. */
enum {
	O_TIME,
	O_DROPRATE,
	O_BOMBS,
	O_RANGE
};


/** Index of the foreground 2 tiles with the given ASCII character. */
enum {
	CH_0       = 0x48, /* '0' */
	CH_1       = 0x49, /* '1' */
	CH_2       = 0x4A, /* '2' */
	CH_3       = 0x4B, /* '3' */
	CH_4       = 0x4C, /* '4' */
	CH_5       = 0x4D, /* '5' */
	CH_6       = 0x4E, /* '6' */
	CH_7       = 0x4F, /* '7' */
	CH_8       = 0x58, /* '8' */
	CH_9       = 0x59, /* '9' */
	CH_P       = 0x5A, /* 'P' */
	CH_percent = 0x5B, /* '%' */
	CH_s       = 0x5C, /* 's' */
	CH_x       = 0x5D, /* 'x' */
	CH_dot     = 0x5E, /* '.' */
	CH_less    = 0x5F, /* '<' */
	CH_space   = 0x00  /* ' ' */
};


/** Index of the player tiles for the given action in `playerTileMap`. */
enum {
	ACT_DOWN = 0,
	ACT_UP   = 3,
	ACT_SIDE = 6
};


/** Index of the foreground 2 tiles for game field elements. */
enum {
	FIELD_EMPTY       = 0x00, /**< empty field */
	FIELD_BOMB_P1     = 0x08, /**< upper left player 1 bomb tile index (frame 1/2) */
	FIELD_BOMB_P2     = 0x0C, /**< upper left player 2 bomb tile index (frame 1/2) */
	FIELD_PU_BOMB     = 0x28, /**< upper left bomb power-up tile index */
	FIELD_PU_RANGE    = 0x2A, /**< upper left range power-up tile index */
	FIELD_PU_SPEED    = 0x2C, /**< upper left speed power-up tile index */
	FIELD_SOLID       = 0x68, /**< upper left solid wall tile index */
	FIELD_BRICKED     = 0x6A, /**< upper left bricked wall tile index (frame 1/3) */
	FIELD_EXPL_MID    = 0x20, /**< upper left explosion center tile index (frame 1/4) */
	FIELD_EXPL_PART_X = 0x40, /**< upper left rightwards explosion part tile index (frame 1/4) */
	FIELD_EXPL_END_X  = 0x60, /**< upper left rightwards explosion end tile index (frame 1/4) */
	FIELD_EXPL_PART_Y = 0x80, /**< upper left downwards explosion part tile index (frame 1/4) */
	FIELD_EXPL_END_Y  = 0xA0, /**< upper left downwards explosion end tile index (frame 1/4) */
	FIELD_TIME        = 0x02, /**< upper left time icon tile index */
	FIELD_PAUSE       = 0x04, /**< upper left pause icon tile index */
	FIELD_TROPHY      = 0x06  /**< upper left trophy icon tile index */
};


/** Mapped index of the foreground 2 tiles for faster categorization. See `fTypeMap`. */
enum {
	FTYPE_EMPTY,
	FTYPE_BOMB_P1,
	FTYPE_BOMB_P2,
	FTYPE_PU_BOMB,
	FTYPE_PU_RANGE,
	FTYPE_PU_SPEED,
	FTYPE_SOLID,
	FTYPE_BRICKED,
	FTYPE_FLAME
};


/** Possible values for `winner`. */
enum {
	WINNER_NA = 0,
	WINNER_P1 = 1,
	WINNER_P2 = 2,
	WINNER_DRAW = WINNER_P1 | WINNER_P2
};


/** Structure holding a single dropped bomb entry. */
typedef struct {
	uint8_t x; /**< upper left tile in x coordinate */
	uint8_t y; /**< upper left tile in y coordinate */
	uint8_t ttl; /**< time to live in 1/10s units */
	uint8_t curFrame; /**< current animation frame index (0..1) */
	uint8_t ttlFrame; /**< time to live until the next animation frame is being shown in 1/10s units */
} tBombField;


/**
 * Structure holding a single entry of a triggered bomb (from a chain reaction).
 */
typedef struct {
	uint8_t range; /**< range of the triggered bomb */
	tBombField * entry; /**< reference to the triggered bomb entry */
} tTriggeredBomb;


/** Structure holding the needed parameters for a single player. */
typedef struct {
	uint8_t x; /**< upper left corner x coordinate (on screen x+8 for easier tile correlation) */
	uint8_t y; /**< upper left corner y coordinate */
	uint8_t firstFrame; /**< first 16x16 frame of the sprite (e.g. ACT_DOWN) */
	uint8_t curFrame; /**< current animation frame index (0..2) */
	uint8_t flipX; /**< flip sprite horizontal? 0 or 1. */
	uint8_t moveAniIdx; /**< used index in `moveAni` */
	uint8_t ttlFrame; /**< time to live until the next animation frame is being shown in 1/10s units */
	uint8_t moving; /**< 1 if the player is moving (requires animation), else 0 */
	uint8_t range; /**< current bomb range */
	uint8_t maxBombs; /**< maximum number of bombs */
	uint8_t bombs; /**< remaining number of bombs */
	uint8_t running; /**< time remaining running */
	tBombField bombList[MAX_BOMBS]; /**< list of active bombs (if `.ttl` > 0) */
	uint8_t lastBombIdx; /**< index to `bombList` for the most recently dropped bomb (if still at that position) */
} tPlayer;


/** Structure holding the first animation frame index and horizontal mirroring information. */
typedef struct {
	uint8_t firstFrame;
	uint8_t flipX;
} tMoveAnimation;


/* forward declarations */
void handleTitle(void);
void handleOptions(void);
void handleGame(void);
void handlePause(void);
void handleWinner(void);


/* external assets (see `bgm1.asm` and `data.asm`) */
#ifdef HAS_BGM
/* soundbank (`bin/bgm1.asm`) */
extern uint8_t SOUNDBANK__0, SOUNDBANK__1;
#endif /* HAS_BGM */
#ifdef HAS_SFX
/* sound effect (`data.asm`) */
extern uint8_t sfx1[], sfx1End[];
#endif /* HAS_SFX */
/* title/options background (`data.asm`) */
extern uint8_t bg1Tiles[], bg1TilesEnd[];
extern uint8_t bg1Pal[], bg1PalEnd[];
extern uint8_t bg1Map[], bg1MapEnd[];
/* field background (`data.asm`) */
extern uint8_t bg2Map[], bg2MapEnd[];
/* credits foreground (`data.asm`) */
extern uint8_t fg1Tiles[], fg1TilesEnd[];
extern uint8_t fg1Pal[], fg1PalEnd[];
extern uint8_t fg1Map[], fg1MapEnd[];
/* options foreground (`data.asm`) */
extern uint8_t optionsMap[], optionsMapEnd[];
/* field foreground (`data.asm`) */
extern uint8_t fg2Tiles[], fg2TilesEnd[];
extern uint8_t fg2Pal[], fg2PalEnd[];
extern uint8_t fieldMap[], fieldMapEnd[];
/* player 1/2 (`data.asm`) */
extern uint8_t p12Tiles[], p12TilesEnd[];
extern uint8_t p12Pal[], p12PalEnd[];


/* global constants */
static const VoidFn screenHandler[] = {
	&handleTitle,
	&handleOptions,
	&handleGame,
	&handlePause,
	&handleWinner
};

static const uint8_t fg2NumText[] = {
	CH_0,
	CH_1,
	CH_2,
	CH_3,
	CH_4,
	CH_5,
	CH_6,
	CH_7,
	CH_8,
	CH_9
};

static const uint8_t optionBelow[] = {
	O_DROPRATE,
	O_BOMBS,
	O_RANGE,
	O_RANGE
};

static const uint8_t optionAbove[] = {
	O_TIME,
	O_TIME,
	O_DROPRATE,
	O_BOMBS
};

/**
 * Contains the `gameFieldLow` and `aniField` index for each game
 * board field (upper left corner) in the game which can change.
 */
static const uint16_t fieldElemIndex[] = {
	/* blocks left untouched during field initialization (see `FIRST_FLEX_FIELD`) */
	TILE_OFFSET_1( 2,  4),
	TILE_OFFSET_1( 2,  6),
	TILE_OFFSET_1( 2,  8),
	TILE_OFFSET_1( 4,  4),
	TILE_OFFSET_1( 6,  4),
	TILE_OFFSET_1(22, 24),
	TILE_OFFSET_1(24, 24),
	TILE_OFFSET_1(26, 20),
	TILE_OFFSET_1(26, 22),
	TILE_OFFSET_1(26, 24),

	/* remaining blocks */
	TILE_OFFSET_1( 2, 10),
	TILE_OFFSET_1( 2, 12),
	TILE_OFFSET_1( 2, 14),
	TILE_OFFSET_1( 2, 16),
	TILE_OFFSET_1( 2, 18),
	TILE_OFFSET_1( 2, 20),
	TILE_OFFSET_1( 2, 22),
	TILE_OFFSET_1( 2, 24),

	TILE_OFFSET_1( 4,  8),
	TILE_OFFSET_1( 4, 12),
	TILE_OFFSET_1( 4, 16),
	TILE_OFFSET_1( 4, 20),
	TILE_OFFSET_1( 4, 24),

	TILE_OFFSET_1( 6,  6),
	TILE_OFFSET_1( 6,  8),
	TILE_OFFSET_1( 6, 10),
	TILE_OFFSET_1( 6, 12),
	TILE_OFFSET_1( 6, 14),
	TILE_OFFSET_1( 6, 16),
	TILE_OFFSET_1( 6, 18),
	TILE_OFFSET_1( 6, 20),
	TILE_OFFSET_1( 6, 22),
	TILE_OFFSET_1( 6, 24),

	TILE_OFFSET_1( 8,  4),
	TILE_OFFSET_1( 8,  8),
	TILE_OFFSET_1( 8, 12),
	TILE_OFFSET_1( 8, 16),
	TILE_OFFSET_1( 8, 20),
	TILE_OFFSET_1( 8, 24),

	TILE_OFFSET_1(10,  4),
	TILE_OFFSET_1(10,  6),
	TILE_OFFSET_1(10,  8),
	TILE_OFFSET_1(10, 10),
	TILE_OFFSET_1(10, 12),
	TILE_OFFSET_1(10, 14),
	TILE_OFFSET_1(10, 16),
	TILE_OFFSET_1(10, 18),
	TILE_OFFSET_1(10, 20),
	TILE_OFFSET_1(10, 22),
	TILE_OFFSET_1(10, 24),

	TILE_OFFSET_1(12,  4),
	TILE_OFFSET_1(12,  8),
	TILE_OFFSET_1(12, 12),
	TILE_OFFSET_1(12, 16),
	TILE_OFFSET_1(12, 20),
	TILE_OFFSET_1(12, 24),

	TILE_OFFSET_1(14,  4),
	TILE_OFFSET_1(14,  6),
	TILE_OFFSET_1(14,  8),
	TILE_OFFSET_1(14, 10),
	TILE_OFFSET_1(14, 12),
	TILE_OFFSET_1(14, 14),
	TILE_OFFSET_1(14, 16),
	TILE_OFFSET_1(14, 18),
	TILE_OFFSET_1(14, 20),
	TILE_OFFSET_1(14, 22),
	TILE_OFFSET_1(14, 24),

	TILE_OFFSET_1(16,  4),
	TILE_OFFSET_1(16,  8),
	TILE_OFFSET_1(16, 12),
	TILE_OFFSET_1(16, 16),
	TILE_OFFSET_1(16, 20),
	TILE_OFFSET_1(16, 24),

	TILE_OFFSET_1(18,  4),
	TILE_OFFSET_1(18,  6),
	TILE_OFFSET_1(18,  8),
	TILE_OFFSET_1(18, 10),
	TILE_OFFSET_1(18, 12),
	TILE_OFFSET_1(18, 14),
	TILE_OFFSET_1(18, 16),
	TILE_OFFSET_1(18, 18),
	TILE_OFFSET_1(18, 20),
	TILE_OFFSET_1(18, 22),
	TILE_OFFSET_1(18, 24),

	TILE_OFFSET_1(20,  4),
	TILE_OFFSET_1(20,  8),
	TILE_OFFSET_1(20, 12),
	TILE_OFFSET_1(20, 16),
	TILE_OFFSET_1(20, 20),
	TILE_OFFSET_1(20, 24),

	TILE_OFFSET_1(22,  4),
	TILE_OFFSET_1(22,  6),
	TILE_OFFSET_1(22,  8),
	TILE_OFFSET_1(22, 10),
	TILE_OFFSET_1(22, 12),
	TILE_OFFSET_1(22, 14),
	TILE_OFFSET_1(22, 16),
	TILE_OFFSET_1(22, 18),
	TILE_OFFSET_1(22, 20),
	TILE_OFFSET_1(22, 22),

	TILE_OFFSET_1(24,  4),
	TILE_OFFSET_1(24,  8),
	TILE_OFFSET_1(24, 12),
	TILE_OFFSET_1(24, 16),
	TILE_OFFSET_1(24, 20),

	TILE_OFFSET_1(26,  4),
	TILE_OFFSET_1(26,  6),
	TILE_OFFSET_1(26,  8),
	TILE_OFFSET_1(26, 10),
	TILE_OFFSET_1(26, 12),
	TILE_OFFSET_1(26, 14),
	TILE_OFFSET_1(26, 16),
	TILE_OFFSET_1(26, 18)
};


/**
 * Maps the animation frame number to the player sprite tile index.
 */
static const uint8_t playerTileMap[] = {
	0x00, 0x02, 0x04, 0x06, 0x08,
	0x0A, 0x0C, 0x0E, 0x20
};


/**
 * Maps the delta movement to the corresponding animation frame.
 * Index = ((dx + 1) << 2) + dy + 1
 */
static const tMoveAnimation moveAni[] = {
	{ACT_UP,   0}, /* -1,-1 */
	{ACT_SIDE, 1}, /* -1, 0 */
	{ACT_DOWN, 0}, /* -1, 1 */
	{ACT_DOWN, 0}, /* invalid */
	{ACT_UP,   0}, /*  0,-1 */
	{ACT_DOWN, 0}, /*  0, 0 */
	{ACT_DOWN, 0}, /*  0, 1 */
	{ACT_DOWN, 0}, /* invalid */
	{ACT_UP,   0}, /*  1,-1 */
	{ACT_SIDE, 0}, /*  1, 0 */
	{ACT_DOWN, 0}  /*  1, 1 */
};


/**
 * Maps the foreground 2 tile index to a field type enumeration value
 * for faster categorization.
 */
static const uint8_t fTypeMap[] = {
	FTYPE_EMPTY, /* 0x00 */
	FTYPE_EMPTY, /* 0x01 */
	FTYPE_EMPTY, /* 0x02 */
	FTYPE_EMPTY, /* 0x03 */
	FTYPE_EMPTY, /* 0x04 */
	FTYPE_EMPTY, /* 0x05 */
	FTYPE_EMPTY, /* 0x06 */
	FTYPE_EMPTY, /* 0x07 */
	FTYPE_BOMB_P1, /* 0x08 */
	FTYPE_BOMB_P1, /* 0x09 */
	FTYPE_BOMB_P1, /* 0x0A */
	FTYPE_BOMB_P1, /* 0x0B */
	FTYPE_BOMB_P2, /* 0x0C */
	FTYPE_BOMB_P2, /* 0x0D */
	FTYPE_BOMB_P2, /* 0x0E */
	FTYPE_BOMB_P2, /* 0x0F */
	FTYPE_EMPTY, /* 0x10 */
	FTYPE_EMPTY, /* 0x11 */
	FTYPE_EMPTY, /* 0x12 */
	FTYPE_EMPTY, /* 0x13 */
	FTYPE_EMPTY, /* 0x14 */
	FTYPE_EMPTY, /* 0x15 */
	FTYPE_EMPTY, /* 0x16 */
	FTYPE_EMPTY, /* 0x17 */
	FTYPE_BOMB_P1, /* 0x18 */
	FTYPE_BOMB_P1, /* 0x19 */
	FTYPE_BOMB_P1, /* 0x1A */
	FTYPE_BOMB_P1, /* 0x1B */
	FTYPE_BOMB_P2, /* 0x1C */
	FTYPE_BOMB_P2, /* 0x1D */
	FTYPE_BOMB_P2, /* 0x1E */
	FTYPE_BOMB_P2, /* 0x1F */
	FTYPE_FLAME, /* 0x20 */
	FTYPE_FLAME, /* 0x21 */
	FTYPE_FLAME, /* 0x22 */
	FTYPE_FLAME, /* 0x23 */
	FTYPE_FLAME, /* 0x24 */
	FTYPE_FLAME, /* 0x25 */
	FTYPE_FLAME, /* 0x26 */
	FTYPE_FLAME, /* 0x27 */
	FTYPE_PU_BOMB, /* 0x28 */
	FTYPE_PU_BOMB, /* 0x29 */
	FTYPE_PU_RANGE, /* 0x2A */
	FTYPE_PU_RANGE, /* 0x2B */
	FTYPE_PU_SPEED, /* 0x2C */
	FTYPE_PU_SPEED, /* 0x2D */
	FTYPE_EMPTY, /* 0x2E */
	FTYPE_EMPTY, /* 0x2F */
	FTYPE_FLAME, /* 0x30 */
	FTYPE_FLAME, /* 0x31 */
	FTYPE_FLAME, /* 0x32 */
	FTYPE_FLAME, /* 0x33 */
	FTYPE_FLAME, /* 0x34 */
	FTYPE_FLAME, /* 0x35 */
	FTYPE_FLAME, /* 0x36 */
	FTYPE_FLAME, /* 0x37 */
	FTYPE_PU_BOMB, /* 0x38 */
	FTYPE_PU_BOMB, /* 0x39 */
	FTYPE_PU_RANGE, /* 0x3A */
	FTYPE_PU_RANGE, /* 0x3B */
	FTYPE_PU_SPEED, /* 0x3C */
	FTYPE_PU_SPEED, /* 0x3D */
	FTYPE_EMPTY, /* 0x3E */
	FTYPE_EMPTY, /* 0x3F */
	FTYPE_FLAME, /* 0x40 */
	FTYPE_FLAME, /* 0x41 */
	FTYPE_FLAME, /* 0x42 */
	FTYPE_FLAME, /* 0x43 */
	FTYPE_FLAME, /* 0x44 */
	FTYPE_FLAME, /* 0x45 */
	FTYPE_FLAME, /* 0x46 */
	FTYPE_FLAME, /* 0x47 */
	FTYPE_EMPTY, /* 0x48 */
	FTYPE_EMPTY, /* 0x49 */
	FTYPE_EMPTY, /* 0x4A */
	FTYPE_EMPTY, /* 0x4B */
	FTYPE_EMPTY, /* 0x4C */
	FTYPE_EMPTY, /* 0x4D */
	FTYPE_EMPTY, /* 0x4E */
	FTYPE_EMPTY, /* 0x4F */
	FTYPE_FLAME, /* 0x50 */
	FTYPE_FLAME, /* 0x51 */
	FTYPE_FLAME, /* 0x52 */
	FTYPE_FLAME, /* 0x53 */
	FTYPE_FLAME, /* 0x54 */
	FTYPE_FLAME, /* 0x55 */
	FTYPE_FLAME, /* 0x56 */
	FTYPE_FLAME, /* 0x57 */
	FTYPE_EMPTY, /* 0x58 */
	FTYPE_EMPTY, /* 0x59 */
	FTYPE_EMPTY, /* 0x5A */
	FTYPE_EMPTY, /* 0x5B */
	FTYPE_EMPTY, /* 0x5C */
	FTYPE_EMPTY, /* 0x5D */
	FTYPE_EMPTY, /* 0x5E */
	FTYPE_EMPTY, /* 0x5F */
	FTYPE_FLAME, /* 0x60 */
	FTYPE_FLAME, /* 0x61 */
	FTYPE_FLAME, /* 0x62 */
	FTYPE_FLAME, /* 0x63 */
	FTYPE_FLAME, /* 0x64 */
	FTYPE_FLAME, /* 0x65 */
	FTYPE_FLAME, /* 0x66 */
	FTYPE_FLAME, /* 0x67 */
	FTYPE_SOLID, /* 0x68 */
	FTYPE_SOLID, /* 0x69 */
	FTYPE_BRICKED, /* 0x6A */
	FTYPE_BRICKED, /* 0x6B */
	FTYPE_BRICKED, /* 0x6C */
	FTYPE_BRICKED, /* 0x6D */
	FTYPE_BRICKED, /* 0x6E */
	FTYPE_BRICKED, /* 0x6F */
	FTYPE_FLAME, /* 0x70 */
	FTYPE_FLAME, /* 0x71 */
	FTYPE_FLAME, /* 0x72 */
	FTYPE_FLAME, /* 0x73 */
	FTYPE_FLAME, /* 0x74 */
	FTYPE_FLAME, /* 0x75 */
	FTYPE_FLAME, /* 0x76 */
	FTYPE_FLAME, /* 0x77 */
	FTYPE_SOLID, /* 0x78 */
	FTYPE_SOLID, /* 0x79 */
	FTYPE_BRICKED, /* 0x7A */
	FTYPE_BRICKED, /* 0x7B */
	FTYPE_BRICKED, /* 0x7C */
	FTYPE_BRICKED, /* 0x7D */
	FTYPE_BRICKED, /* 0x7E */
	FTYPE_BRICKED, /* 0x7F */
	FTYPE_FLAME, /* 0x80 */
	FTYPE_FLAME, /* 0x81 */
	FTYPE_FLAME, /* 0x82 */
	FTYPE_FLAME, /* 0x83 */
	FTYPE_FLAME, /* 0x84 */
	FTYPE_FLAME, /* 0x85 */
	FTYPE_FLAME, /* 0x86 */
	FTYPE_FLAME, /* 0x87 */
	FTYPE_EMPTY, /* 0x88 */
	FTYPE_EMPTY, /* 0x89 */
	FTYPE_EMPTY, /* 0x8A */
	FTYPE_EMPTY, /* 0x8B */
	FTYPE_EMPTY, /* 0x8C */
	FTYPE_EMPTY, /* 0x8D */
	FTYPE_EMPTY, /* 0x8E */
	FTYPE_EMPTY, /* 0x8F */
	FTYPE_FLAME, /* 0x90 */
	FTYPE_FLAME, /* 0x91 */
	FTYPE_FLAME, /* 0x92 */
	FTYPE_FLAME, /* 0x93 */
	FTYPE_FLAME, /* 0x94 */
	FTYPE_FLAME, /* 0x95 */
	FTYPE_FLAME, /* 0x96 */
	FTYPE_FLAME, /* 0x97 */
	FTYPE_EMPTY, /* 0x98 */
	FTYPE_EMPTY, /* 0x99 */
	FTYPE_EMPTY, /* 0x9A */
	FTYPE_EMPTY, /* 0x9B */
	FTYPE_EMPTY, /* 0x9C */
	FTYPE_EMPTY, /* 0x9D */
	FTYPE_EMPTY, /* 0x9E */
	FTYPE_EMPTY, /* 0x9F */
	FTYPE_FLAME, /* 0xA0 */
	FTYPE_FLAME, /* 0xA1 */
	FTYPE_FLAME, /* 0xA2 */
	FTYPE_FLAME, /* 0xA3 */
	FTYPE_FLAME, /* 0xA4 */
	FTYPE_FLAME, /* 0xA5 */
	FTYPE_FLAME, /* 0xA6 */
	FTYPE_FLAME, /* 0xA7 */
	FTYPE_EMPTY, /* 0xA8 */
	FTYPE_EMPTY, /* 0xA9 */
	FTYPE_EMPTY, /* 0xAA */
	FTYPE_EMPTY, /* 0xAB */
	FTYPE_EMPTY, /* 0xAC */
	FTYPE_EMPTY, /* 0xAD */
	FTYPE_EMPTY, /* 0xAE */
	FTYPE_EMPTY, /* 0xAF */
	FTYPE_FLAME, /* 0xB0 */
	FTYPE_FLAME, /* 0xB1 */
	FTYPE_FLAME, /* 0xB2 */
	FTYPE_FLAME, /* 0xB3 */
	FTYPE_FLAME, /* 0xB4 */
	FTYPE_FLAME, /* 0xB5 */
	FTYPE_FLAME, /* 0xB6 */
	FTYPE_FLAME, /* 0xB7 */
	FTYPE_EMPTY, /* 0xB8 */
	FTYPE_EMPTY, /* 0xB9 */
	FTYPE_EMPTY, /* 0xBA */
	FTYPE_EMPTY, /* 0xBB */
	FTYPE_EMPTY, /* 0xBC */
	FTYPE_EMPTY, /* 0xBD */
	FTYPE_EMPTY, /* 0xBE */
	FTYPE_EMPTY  /* 0xBF */
};


/* global variables */
static uint8_t i, j, j2;             /* 8-bit loop variables */
static uint16_t k, m;                /* 16-bit loop variables */
static uint16_t gameOver;            /* time remaining until the end of the game in seconds */
static uint8_t winner;               /* hold the winner or 0 if not yet decided */
static uint8_t screen, option;       /* current screen/option */
static uint16_t pad0, pad1;          /* current pad values */
static uint16_t * pausePad;          /* pad that issued the game pause */
static tPlayer p1, p2;               /* player specific parameters */
static tTriggeredBomb bombChain[MAX_BOMBS * 2]; /* list of chain triggered bombs */
static uint8_t bombChainCount;       /* number of valid items in `bombChain` */
static int8_t dx, dy, ds;            /* player movement (delta x, delta y, delta step; allowed values for dx/dy: -1, 0, 1) */
static uint8_t x, y;                 /* player reference tile coordinates for collision detection */
static uint8_t x1, y1;               /* helper variables for player collision detection */
static uint8_t x2, y2;               /* helper variables for player collision detection */
static uint8_t * field;              /* pointer to the upper left game field tile */
static uint8_t * attrField;          /* pointer to the upper left game field tile attribute */
static bool b;                       /* boolean used in bgSlideIn/bgSlideOut */
static uint8_t digits[5];            /* number conversion array */
static uint8_t gameFieldLow[32*28];  /* in-memory game screen tile indices to decouple VRAM access (only low byte values) */
static uint8_t gameFieldHigh[32*28]; /* in-memory game screen tile attributes to decouple VRAM access (only high byte values) */
static uint8_t aniField[32*28];      /* animation frame index of each game field */
static uint8_t ttlField[32*28];      /* animation frame time to live of each game field */
static uint8_t framesUntil10Hz;      /* remaining frames until next 10Hz tick */
static uint16_t counter10Hz;         /* 10Hz counter */
static uint8_t untilSecond;          /* 10Hz ticks until next full second */
static bool refreshGameScreenLow;    /* need low byte game screen low bytes refresh? */
static bool refreshGameScreenHigh;   /* need low byte game screen high bytes refresh? */
static bool refreshSprites;          /* need to update the sprite object attribute data? */
brrsamples sfx1Sample[1];            /* sound effect sample */
/* configuration */
static uint16_t maxTime;
static uint8_t dropRate, dropRate255;
static uint8_t maxBombs, maxRange;


/**
 * Converts the given number to digits and stores the result in `digits`.
 *
 * @param[in] value - number to convert
 * @remarks sets `i` to the number of digits
 */
static void convertNumber(uint16_t value) {
	/* convert number to digits */
	i = 0;
	do {
		ASSERT_ARY_IDX(digits, i);
		ASSERT_ARY_IDX(fg2NumText, value % 10);
		digits[i] = fg2NumText[value % 10];
		value /= 10;
		++i;
	} while (value != 0);
}


/**
 * Writes a numeric value with unit and padded end using foreground 2 tiles.
 *
 * @param[in] index - `gameFieldLow` index
 * @param[in] chars - number of characters to write (padded with spaces at the end)
 * @param[in] value - numeric value to write
 * @param[in] unit - number unit (single character)
 * @remarks Should be called during V-blank to avoid glitches.
 */
static void writeNumWithUnit(uint16_t index, uint8_t chars, uint16_t value, const uint8_t unit) {
	convertNumber(value);
	/* write digits */
	while (chars != 0 && i != 0) {
		--i;
		ASSERT_ARY_IDX(digits, i);
		ASSERT_ARY_IDX(gameFieldLow, index);
		gameFieldLow[index++] = digits[i];
		--chars;
	}
	/* write unit */
	if (chars != 0) {
		ASSERT_ARY_IDX(gameFieldLow, index);
		gameFieldLow[index++] = unit;
		--chars;
	}
	/* pad remaining with spaces */
	while (chars != 0) {
		ASSERT_ARY_IDX(gameFieldLow, index);
		gameFieldLow[index++] = CH_space;
		--chars;
	}
}


/**
 * Writes a numeric value with unit and padded end using foreground 2 tiles.
 *
 * @param[in] address - VRAM address (word offset)
 * @param[in] chars - number of characters to write (padded with spaces at the end)
 * @param[in] value - numeric value to write
 * @param[in] unit - number unit (single character)
 * @param[in] select - selection mark (single character)
 * @remarks Should be called during V-blank to avoid glitches.
 */
static void writeVramNumWithUnit(const uint16_t address, uint8_t chars, uint16_t value, const uint8_t unit, const uint8_t select) {
	convertNumber(value);
	/* increment VRAM address after each low byte write (REG_VMDATAL) */
	REG_VMAIN = 0x00;
	REG_VMADDLH = address;
	/* write digits */
	while (chars != 0 && i != 0) {
		--i;
		ASSERT_ARY_IDX(digits, i);
		REG_VMDATAL = digits[i];
		--chars;
	}
	/* write unit */
	if (chars != 0) {
		REG_VMDATAL = unit;
		--chars;
	}
	/* write selection mark */
	if (chars != 0) {
		REG_VMDATAL = select;
		--chars;
	}
	/* pad remaining with spaces */
	while (chars != 0) {
		REG_VMDATAL = CH_space;
		--chars;
	}
}


/**
 * Sets the X offset for all sprites.
 *
 * @param[in] offset - new x offset
 */
static inline void setSpritesOffsetX(const uint16_t offset) {
	oamSetXY(P1_NR, p1.x + offset + 8, p1.y);
	oamSetXY(P2_NR, p2.x + offset + 8, p2.y);
}


/**
 * Perform a slide-in of the given SNES background.
 * Set bgNum1 to INVALID_NR to slide only one SNES background.
 *
 * @param[in] bgNum0 - foreground number (0 to 3)
 * @param[in] bgNum1 - background number (0 to 3)
 * @param[in] sprites - slide-in sprites as well?
 */
static void bgSlideIn(const uint8_t bgNum0, const uint8_t bgNum1, const bool sprites) {
	WaitForVBlank();
	for (k = 0, b = false; !b; k += SLIDE_SPEED) {
		if (k >= 256) {
			bgSetScroll(bgNum0, 256, VERT_OFFSET);
			if (bgNum1 != INVALID_NR) {
				bgSetScroll(bgNum1, 256, VERT_OFFSET);
			}
			if ( sprites ) {
				setSpritesOffsetX(0);
			}
			b = true;
		} else {
			bgSetScroll(bgNum0, k, VERT_OFFSET);
			if (bgNum1 != INVALID_NR) {
				bgSetScroll(bgNum1, k, VERT_OFFSET);
			}
			if ( sprites ) {
				setSpritesOffsetX(256 - k);
			}
		}
		if ( sprites ) {
			/* avoid delay by one frame */
			oamUpdate();
		}
		WaitForVBlank();
	}
}


/**
 * Perform a slide-out of the given SNES background.
 * Set bgNum1 to INVALID_NR to slide only one SNES background.
 *
 * @param[in] bgNum0 - background number (0 to 3)
 * @param[in] bgNum1 - background number (0 to 3)
 * @param[in] sprites - slide-in sprites as well?
 */
static void bgSlideOut(const uint8_t bgNum0, const uint8_t bgNum1, const bool sprites) {
	WaitForVBlank();
	for (k = 256, b = false; !b; k -= SLIDE_SPEED) {
		if (k > 256) {
			bgSetScroll(bgNum0, 0, VERT_OFFSET);
			if (bgNum1 != INVALID_NR) {
				bgSetScroll(bgNum1, 0, VERT_OFFSET);
			}
			if ( sprites ) {
				setSpritesOffsetX(256);
			}
			b = true;
		} else {
			bgSetScroll(bgNum0, k, VERT_OFFSET);
			if (bgNum1 != INVALID_NR) {
				bgSetScroll(bgNum1, k, VERT_OFFSET);
			}
			if ( sprites ) {
				setSpritesOffsetX(256 - k);
			}
		}
		if ( sprites ) {
			/* avoid delay by one frame */
			oamUpdate();
		}
		WaitForVBlank();
	}
}


/**
 * Updates the shown configuration values on the options screen.
 */
static void updateOptionsScreen(void) {
	WaitForVBlank(); /* ensure access to VRAM */
	writeVramNumWithUnit(WORD_OFFSET(MAP_VRAM_FG + MAP_PAGE_SIZE) + TILE_OFFSET(10, 11), 5, maxTime,  CH_s,       (option == O_TIME)     ? CH_less : CH_space);
	writeVramNumWithUnit(WORD_OFFSET(MAP_VRAM_FG + MAP_PAGE_SIZE) + TILE_OFFSET(10, 14), 5, dropRate, CH_percent, (option == O_DROPRATE) ? CH_less : CH_space);
	writeVramNumWithUnit(WORD_OFFSET(MAP_VRAM_FG + MAP_PAGE_SIZE) + TILE_OFFSET(10, 17), 3, maxBombs, CH_x,       (option == O_BOMBS)    ? CH_less : CH_space);
	writeVramNumWithUnit(WORD_OFFSET(MAP_VRAM_FG + MAP_PAGE_SIZE) + TILE_OFFSET(10, 20), 3, maxRange, CH_x,       (option == O_RANGE)    ? CH_less : CH_space);
}


/**
 * Change the clock icon on the game screen.
 *
 * @param[in] stopIcon - false for clock, true for stop icon
 */
static void changeClockIcon(const bool stopIcon) {
	/* increment VRAM address after each low byte write (REG_VMDATAL) */
	REG_VMAIN = 0x00;
	REG_VMADDLH = WORD_OFFSET(MAP_VRAM_FG + MAP_PAGE_SIZE) + TILE_OFFSET(13, 0);
	if ( stopIcon ) {
		REG_VMDATAL = FIELD_PAUSE + 0x00;
		REG_VMDATAL = FIELD_PAUSE + 0x01;
		REG_VMADDLH = WORD_OFFSET(MAP_VRAM_FG + MAP_PAGE_SIZE) + TILE_OFFSET(13, 1);
		REG_VMDATAL = FIELD_PAUSE + 0x10;
		REG_VMDATAL = FIELD_PAUSE + 0x11;
	} else {
		REG_VMDATAL = FIELD_TIME + 0x00;
		REG_VMDATAL = FIELD_TIME + 0x01;
		REG_VMADDLH = WORD_OFFSET(MAP_VRAM_FG + MAP_PAGE_SIZE) + TILE_OFFSET(13, 1);
		REG_VMDATAL = FIELD_TIME + 0x10;
		REG_VMDATAL = FIELD_TIME + 0x11;
	}
}


/**
 * Updates the player sprites object attributes from `p1` and `p2`.
 */
static void updatePlayerSprites(void) {
	ASSERT_ARY_IDX(playerTileMap, p1.curFrame);
	ASSERT_ARY_IDX(playerTileMap, p2.curFrame);
	oamSet(P1_NR, p1.x + 8, p1.y, 3, p1.flipX, 0, playerTileMap[p1.curFrame], 4);
	oamSet(P2_NR, p2.x + 8, p2.y, 3, p2.flipX, 0, playerTileMap[p2.curFrame], 5);
	refreshSprites = false;
}


/**
 * Initializes the in-memory game field data.
 */
static void initializeGame(void) {
	/* copy tile indices of tile map from ROM */
	for (k = 0, m = 0; m < ARRAY_SIZE(gameFieldLow); k += 2, ++m) {
		ASSERT_ARY_IDX(gameFieldLow, m);
		ASSERT_ARY_IDX(gameFieldHigh, m);
		ASSERT_ARY_IDX(aniField, m);
		ASSERT_ARY_IDX(ttlField, m);
		gameFieldLow[m]  = fieldMap[k + 0]; /* tile index only (low byte) */
		gameFieldHigh[m] = fieldMap[k + 1]; /* tile attributes only (high byte) */
		aniField[m] = 0; /* initial animation frame number */
		ttlField[m] = 0; /* initial animation time to live value */
	}
	/* randomize wall setup */
	*((uint16_t *)(&lrngSeed)) = snes_vblank_count | 0x40; /* initialize seed; ensure != zero */
	for (i = FIRST_FLEX_FIELD; i < ARRAY_SIZE(fieldElemIndex); ++i) {
		if ((lrng() & 7) >= 6) {
			/* this field is not a wall (probability of 1/4) -> clear it */
			field = gameFieldLow + fieldElemIndex[i];
			clearField(field);
		}
	}
	/* initialize related variables */
	p1.bombs = p2.bombs = 1;
	p1.maxBombs = p2.maxBombs = 1;
	p1.range = p2.range = 1;
	p1.running = p2.running = 0;
	p1.firstFrame = p2.firstFrame = ACT_DOWN;
	p1.curFrame = p2.curFrame = ACT_DOWN;
	p1.flipX = p2.flipX = 0;
	p1.moveAniIdx = p2.moveAniIdx = 5;
	p1.moving = p2.moving = 0;
	p1.x = 2 * 8;
	p1.y = 4 * 8;
	p2.x = 26 * 8;
	p2.y = 24 * 8;
	for (i = 0; i < MAX_BOMBS; ++i) {
		ASSERT_ARY_IDX(p1.bombList, i);
		ASSERT_ARY_IDX(p2.bombList, i);
		p1.bombList[i].ttl = 0;
		p2.bombList[i].ttl = 0;
	}
	p1.lastBombIdx = p2.lastBombIdx = INVALID_LAST_BOMB_IDX;
	dropRate255 = (uint8_t)((((uint16_t)dropRate) * 255) / 100);
	gameOver = maxTime;
	winner = WINNER_NA;
	/* update remaining time on screen */
	writeNumWithUnit(TILE_OFFSET(15, 1), 4, gameOver, CH_s);
	/* update and show player sprites */
	updatePlayerSprites();
	/* the game screen is not yet visible */
	setSpritesOffsetX(256);
	oamSetVisible(P1_NR, OBJ_HIDE);
	oamSetVisible(P2_NR, OBJ_HIDE);
}


/**
 * Tests whether the game field at `x`, `y` can be
 * entered. This is true if the field is empty, a
 * power-up or the bomb just placed.
 *
 * @param[in] player - instance of the player which tries to enter the field
 * @return true if accessible, else false
 */
static inline bool canEnter(const tPlayer * player) {
	ASSERT_ARY_IDX(gameFieldLow, TILE_OFFSET_1(x, y));
	ASSERT_ARY_IDX(fTypeMap, gameFieldLow[TILE_OFFSET_1(x, y)]);
	switch (fTypeMap[gameFieldLow[TILE_OFFSET_1(x, y)]]) {
	case FTYPE_EMPTY:
	case FTYPE_PU_BOMB:
	case FTYPE_PU_RANGE:
	case FTYPE_PU_SPEED:
	case FTYPE_FLAME:
		return true;
	case FTYPE_BOMB_P1:
	case FTYPE_BOMB_P2:
		if (player->lastBombIdx != INVALID_LAST_BOMB_IDX && player->bombList[player->lastBombIdx].ttl > 0 && player->bombList[player->lastBombIdx].x == x && player->bombList[player->lastBombIdx].y == y) {
			ASSERT_ARY_IDX(player->bombList, player->lastBombIdx);
			return true;
		}
		/* fall-through */
	default:
		return false;
	}
}


/**
 * Handle the title screen and related events.
 */
void handleTitle(void) {
	if (pad0 & (KEY_START | KEY_A)) {
		/* change to option screen */
		bgSlideOut(FG_NR, INVALID_NR, false);
		screen = S_OPTIONS;
		option = O_TIME;
		/* replace hidden second page */
		dmaCopyVram(optionsMap, WORD_OFFSET(MAP_VRAM_FG + MAP_PAGE_SIZE), MAP_PAGE_SIZE);
		/* use `fg2Tiles` for the foreground map */
		updateOptionsScreen();
		bgSetGfxPtr(FG_NR, WORD_OFFSET(CHR_VRAM_FG2));
		bgSlideIn(FG_NR, INVALID_NR, false);
	}
}


/**
 * Handle the title options and related events.
 */
void handleOptions(void) {
	if (pad0 & (uint16_t)(KEY_SELECT | KEY_B)) {
		/* change to title screen */
		bgSlideOut(FG_NR, INVALID_NR, false);
		screen = S_TITLE;
		option = O_TIME;
		/* replace second page */
		dmaCopyVram(fg1Map, WORD_OFFSET(MAP_VRAM_FG + MAP_PAGE_SIZE), MAP_PAGE_SIZE);
		/* use `fg1Tiles` for the foreground map */
		bgSetGfxPtr(FG_NR, WORD_OFFSET(CHR_VRAM_FG1));
		bgSlideIn(FG_NR, INVALID_NR, false);
	} else if (pad0 & (KEY_START | KEY_A)) {
		/* change to game screen */
		bgSlideOut(FG_NR, BG_NR, false);
		screen = S_GAME;
		framesUntil10Hz = FP10HZ;
		untilSecond = 10;
		refreshGameScreenLow = false;
		/* replace hidden second page */
		dmaCopyVram(bg2Map, WORD_OFFSET(MAP_VRAM_BG + MAP_PAGE_SIZE), MAP_PAGE_SIZE);
		dmaCopyVram(fieldMap, WORD_OFFSET(MAP_VRAM_FG + MAP_PAGE_SIZE), MAP_PAGE_SIZE);
		initializeGame();
		WaitForVBlank(); /* ensure access to VRAM is possible */
		/* update game field (only the tile indices) */
		dmaCopyVramLowBytes(gameFieldLow, WORD_OFFSET(MAP_VRAM_FG + MAP_PAGE_SIZE), sizeof(gameFieldLow));
		bgSlideIn(FG_NR, BG_NR, true);
	} else if (pad0 & KEY_DOWN) {
		/* select option below */
		ASSERT_ARY_IDX(optionBelow, option);
		option = optionBelow[option];
		updateOptionsScreen();
		clickDelay();
	} else if (pad0 & KEY_UP) {
		/* select option above */
		ASSERT_ARY_IDX(optionAbove, option);
		option = optionAbove[option];
		updateOptionsScreen();
		clickDelay();
	} else if (pad0 & KEY_LEFT) {
		/* decrease selected value */
		switch (option) {
		case O_TIME:
			if (maxTime > 60) {
				maxTime -= 10;
			}
			break;
		case O_DROPRATE:
			if (dropRate > 0) {
				dropRate -= 5;
			}
			break;
		case O_BOMBS:
			if (maxBombs > 1) {
				--maxBombs;
			}
			break;
		case O_RANGE:
			if (maxRange > 1) {
				--maxRange;
			}
			break;
		default:
			break;
		}
		updateOptionsScreen();
		clickDelay();
	} else if (pad0 & KEY_RIGHT) {
		/* increase selected value */
		switch (option) {
		case O_TIME:
			if (maxTime < 990) {
				maxTime += 10;
			}
			break;
		case O_DROPRATE:
			if (dropRate < 100) {
				dropRate += 5;
			}
			break;
		case O_BOMBS:
			if (maxBombs < MAX_BOMBS) {
				++maxBombs;
			}
			break;
		case O_RANGE:
			if (maxRange < MAX_RANGE) {
				++maxRange;
			}
			break;
		default:
			break;
		}
		updateOptionsScreen();
		clickDelay();
	} else if (pad0 & KEY_X) {
		/* reset selected value to its defaults */
		switch (option) {
		case O_TIME:
			maxTime = DEF_MAX_TIME;
			break;
		case O_DROPRATE:
			dropRate = DEF_DROP_RATE;
			break;
		case O_BOMBS:
			maxBombs = DEF_MAX_BOMBS;
			break;
		case O_RANGE:
			maxRange = DEF_MAX_RANGE;
			break;
		default:
			break;
		}
		updateOptionsScreen();
		clickDelay();
	}
}


/**
 * Checks whether the player is over an ongoing explosion or power-up field.
 *
 * @param[in] player - instance of the player in question
 * @param[in] xOffset - x offset from the upper left corner of the player sprite
 * @param[in] yOffset - y offset from the upper left corner of the player sprite
 */
static void checkPlayerCollision(tPlayer * player, const uint8_t xOffset, const uint8_t yOffset) {
	x = ((uint8_t)(player->x + xOffset) >> 3) & ~1;
	y = ((uint8_t)(player->y + yOffset) >> 3) & ~1;
	field = gameFieldLow + TILE_OFFSET_1(x, y);
	ASSERT_ARY_PTR(gameFieldLow, field);
	ASSERT_ARY_IDX(fTypeMap, *field);
	switch (fTypeMap[*field]) {
	case FTYPE_PU_BOMB:
		if (player->maxBombs < maxBombs) {
			++player->bombs;
			++player->maxBombs;
			ASSERT(player->bombs <= player->maxBombs);
			/* update shown stats */
			if (player == &p1) {
				x = 3;
			} else {
				x = 23;
			}
			ASSERT_ARY_IDX(fg2NumText, player->maxBombs);
			ASSERT_ARY_IDX(gameFieldLow, TILE_OFFSET(x, 1));
			gameFieldLow[TILE_OFFSET(x, 1)] = fg2NumText[player->maxBombs];
			refreshGameScreenLow = true;
		}
		goto consumed;
	case FTYPE_PU_RANGE:
		if (player->range < maxRange) {
			++player->range;
			/* update shown stats */
			if (player == &p1) {
				x = 7;
			} else {
				x = 27;
			}
			ASSERT_ARY_IDX(fg2NumText, player->range);
			ASSERT_ARY_IDX(gameFieldLow, TILE_OFFSET(x, 1));
			gameFieldLow[TILE_OFFSET(x, 1)] = fg2NumText[player->range];
			refreshGameScreenLow = true;
		}
		goto consumed;
	case FTYPE_PU_SPEED:
		player->running = BOOTS_TTL;
		goto consumed;
	case FTYPE_FLAME:
		if (player == &p1) {
			winner |= WINNER_P2;
		} else {
			winner |= WINNER_P1;
		}
		break;
	default:
		break;
	}
	return;
consumed:
	clearField(field);
}


/**
 * Handles the input events of a single player.
 *
 * @param[in] pad - associated pad values
 * @param[in] player - related player instance
 */
static void handlePlayer(const uint16_t pad, tPlayer * player) {
	/* handle bomb drop */
	if ((pad & KEY_A) && player->bombs) {
		/* drop a bomb */
		x = ((uint8_t)(player->x + P_MID_X) >> 3) & ~1;
		y = ((uint8_t)(player->y + P_MID_Y) >> 3) & ~1;
		field = gameFieldLow + TILE_OFFSET_1(x, y);
		ASSERT_ARY_PTR(gameFieldLow, field);
		if (*field == FIELD_EMPTY) {
			/* empty field -> drop bomb */
			--player->bombs;
			ASSERT(player->bombs <= player->maxBombs);
			setField(field, FIELD_BOMB_P1);
#ifndef NDEBUG
			b = false;
#endif /* NDEBUG */
			for (i = 0; i < MAX_BOMBS; ++i) {
				ASSERT_ARY_IDX(player->bombList, i);
				if (player->bombList[i].ttl == 0) {
					player->bombList[i].x = x;
					player->bombList[i].y = y;
					player->bombList[i].ttl = BOMB_TTL;
					player->bombList[i].curFrame = 0;
					player->bombList[i].ttlFrame = BOMB_ANIMATION;
					player->lastBombIdx = i;
#ifndef NDEBUG
					b = true;
#endif /* NDEBUG */
					break;
				}
			}
			ASSERT(b);
			ASSERT_ARY_IDX(player->bombList, player->lastBombIdx);
		}
	}
	/* handle player movement */
	ds = player->running ? 2 : 1;
	for (i = 0; i < ds; ++i) {
		dx = dy = 0;
		if (pad & KEY_LEFT) {
			--dx;
		}
		if (pad & KEY_RIGHT) {
			++dx;
		}
		if (pad & KEY_UP) {
			--dy;
		}
		if (pad & KEY_DOWN) {
			++dy;
		}
		/* change x if not blocked (check collision with upper/lower 16x16 block) */
		j = (uint8_t)(player->x + dx);
		if (dx > 0) {
			j2 = (uint8_t)(j + P_RIGHT);
			x = (j2 >> 3) & ~1;
			y = ((uint8_t)(player->y + P_TOP) >> 3) & ~1;
			b = canEnter(player);
			y = ((uint8_t)(player->y + P_BOTTOM) >> 3) & ~1;
			if (b && canEnter(player)) {
				player->x = j;
				refreshSprites = true;
			}
		} else if (dx < 0) {
			j2 = (uint8_t)(j + P_LEFT);
			x = (j2 >> 3) & ~1;
			y = ((uint8_t)(player->y + P_TOP) >> 3) & ~1;
			b = canEnter(player);
			y = ((uint8_t)(player->y + P_BOTTOM) >> 3) & ~1;
			if (b && canEnter(player)) {
				player->x = j;
				refreshSprites = true;
			}
		}
		/* change y if not blocked (check collision with left/right 16x16 block) */
		j = (uint8_t)(player->y + dy);
		if (dy > 0) {
			j2 = (uint8_t)(j + P_BOTTOM);
			x = ((uint8_t)(player->x + P_LEFT) >> 3) & ~1;
			y = (j2 >> 3) & ~1;
			b = canEnter(player);
			x = ((uint8_t)(player->x + P_RIGHT) >> 3) & ~1;
			if (b && canEnter(player)) {
				player->y = j;
				refreshSprites = true;
			}
		} else if (dy < 0) {
			j2 = (uint8_t)(j + P_TOP);
			x = ((uint8_t)(player->x + P_LEFT) >> 3) & ~1;
			y = (j2 >> 3) & ~1;
			b = canEnter(player);
			x = ((uint8_t)(player->x + P_RIGHT) >> 3) & ~1;
			if (b && canEnter(player)) {
				player->y = j;
				refreshSprites = true;
			}
		}
		/* animation handling */
		if ( player->moving ) {
			if (dx != 0 || dy != 0) {
				/* keep moving; update direction */
				j = (uint8_t)((uint8_t)((uint8_t)(dx + 1) << 2) + (uint8_t)(dy + 1));
				if (player->moveAniIdx != j) {
					/* direction changed */
					ASSERT_ARY_IDX(moveAni, j);
					player->firstFrame = moveAni[j].firstFrame;
					player->curFrame = player->firstFrame;
					player->flipX = moveAni[j].flipX;
					player->moveAniIdx = j;
					player->ttlFrame = PLAYER_ANIMATION;
					refreshSprites = true;
				}
			} else {
				/* stopped moving; keep direction */
				player->curFrame = player->firstFrame;
				player->moving = 0;
				refreshSprites = true;
			}
		} else if (dx != 0 || dy != 0) {
			/* started moving */
			player->moving = 1;
		}
		if ( player->moving ) {
			/* exit from last bomb dropped handling */
			if (player->lastBombIdx != INVALID_LAST_BOMB_IDX) {
				ASSERT_ARY_IDX(player->bombList, player->lastBombIdx);
				x = player->bombList[player->lastBombIdx].x;
				y = player->bombList[player->lastBombIdx].y;
				x1 = ((uint8_t)(player->x + P_LEFT)   >> 3) & ~1;
				x2 = ((uint8_t)(player->x + P_RIGHT)  >> 3) & ~1;
				y1 = ((uint8_t)(player->y + P_TOP)    >> 3) & ~1;
				y2 = ((uint8_t)(player->y + P_BOTTOM) >> 3) & ~1;
				if ( ! (x1 <= x && x2 >= x && y1 <= y && y2 >= y) ) {
					/* not over the last dropped bomb anymore -> disallow re-entering this field */
					player->lastBombIdx = INVALID_LAST_BOMB_IDX;
				}
			}
		}
		checkPlayerCollision(player, P_LEFT, P_TOP);
		checkPlayerCollision(player, P_RIGHT, P_TOP);
		checkPlayerCollision(player, P_LEFT, P_BOTTOM);
		checkPlayerCollision(player, P_RIGHT, P_BOTTOM);
	}
}


/**
 * Handles a single exploded field. Uses the global
 * variables `x`, `y` as input for the coordinates
 * and `m` for the game field index. It also used
 * `j` to determine if this was the last exploded
 * field for the given direction.
 *
 * @param[in] attr - explosion field attribute
 * @param[in] part - explosion part tile index
 * @param[in] end - explosion end tile index
 * @return true to continue, else false if the explosion was blocked
 * @remarks Uses `field` and `j2` internally.
 */
static bool handleExplodedField(const uint8_t attr, const uint8_t part, const uint8_t end) {
	ASSERT((x & 1) == 0);
	ASSERT((y & 1) == 0);
	ASSERT_ARY_IDX(gameFieldLow, m);
	field = gameFieldLow + m;
	ASSERT_ARY_IDX(fTypeMap, *field);
	switch (fTypeMap[*field]) {
	case FTYPE_EMPTY:
		ASSERT_ARY_IDX(aniField, m);
		aniField[m] = 4;
		ttlField[m] = EXPLOSION_ANIMATION;
		attrField = gameFieldHigh + m;
		setFieldAttr(attrField, attr);
		j2 = (j == 1) ? end : part;
		if (attr & 0x80) {
			setFieldFlippedY(field, j2);
		} else if (attr & 0x40) {
			setFieldFlippedX(field, j2);
		} else {
			setField(field, j2);
		}
		return true;
	case FTYPE_FLAME:
		/* crossing other explosion */
		ASSERT_ARY_IDX(aniField, m);
		attrField = gameFieldHigh + m;
		setFieldAttr(attrField, TILE_ATTR(0, 0, 1, 3));
		setField(field, FIELD_EXPL_MID + (2 * (4 - aniField[m])));
		return true;
	case FTYPE_PU_BOMB:
	case FTYPE_PU_RANGE:
	case FTYPE_PU_SPEED:
		/* explosion ends here but destroys the power-up */
		clearField(field);
		/* fall-through */
	case FTYPE_SOLID:
		break;
	case FTYPE_BOMB_P1:
		/* chain reaction (may not trigger on circular chains) */
		for (j2 = 0; j2 < MAX_BOMBS; ++j2) {
			ASSERT_ARY_IDX(p1.bombList, j2);
			if (p1.bombList[j2].ttl && p1.bombList[j2].x == x && p1.bombList[j2].y == y) {
				++p1.bombs;
				ASSERT(p1.bombs <= p1.maxBombs);
				p1.bombList[j2].ttl = 0; /* prevent timeout to trigger also */
				bombChain[bombChainCount].range = p1.range;
				bombChain[bombChainCount].entry = p1.bombList + j2;
				++bombChainCount;
				break;
			}
		}
		break;
	case FTYPE_BOMB_P2:
		/* chain reaction (may not trigger on circular chains) */
		for (j2 = 0; j2 < MAX_BOMBS; ++j2) {
			ASSERT_ARY_IDX(p2.bombList, j2);
			if (p2.bombList[j2].ttl && p2.bombList[j2].x == x && p2.bombList[j2].y == y) {
				++p2.bombs;
				ASSERT(p2.bombs <= p2.maxBombs);
				p2.bombList[j2].ttl = 0; /* prevent timeout to trigger also */
				bombChain[bombChainCount].range = p2.range;
				bombChain[bombChainCount].entry = p2.bombList + j2;
				++bombChainCount;
				break;
			}
		}
		break;
	case FTYPE_BRICKED:
		ASSERT_ARY_IDX(aniField, m);
		if (aniField[m] == 0) {
			/* if not already hit */
			aniField[m] = 4;
			ttlField[m] = EXPLOSION_ANIMATION;
			setField(field, FIELD_BRICKED + 2);
		}
		break;
	default:
		break;
	}
	return false;
}


/**
 * Handles the explosion of a bomb at the given bomb entry.
 *
 * @param[in] range - explosion range
 * @param[in,out] bombEntry - player bomb list entry pointer
 */
static void handleExplosion(const uint8_t range, tBombField * bombEntry) {
	x = bombEntry->x;
	y = bombEntry->y;
	k = TILE_OFFSET_1(x, y);
	ASSERT_ARY_IDX(aniField, k);
	ASSERT_ARY_IDX(ttlField, k);
	aniField[k] = 4;
	ttlField[k] = EXPLOSION_ANIMATION;
	field = gameFieldLow + k;
	setField(field, FIELD_EXPL_MID);
	/* going left */
	for (j = range, m = k; j; --j) {
		x -= 2; /* previous column */
		m -= 2; /* previous column */
		if ( ! handleExplodedField(TILE_ATTR(0, 1, 1, 3), FIELD_EXPL_PART_X, FIELD_EXPL_END_X) ) {
			break;
		}
	}
	x = bombEntry->x;
	/* going right */
	for (j = range, m = k; j; --j) {
		x += 2; /* next column */
		m += 2; /* next column */
		if ( ! handleExplodedField(TILE_ATTR(0, 0, 1, 3), FIELD_EXPL_PART_X, FIELD_EXPL_END_X) ) {
			break;
		}
	}
	x = bombEntry->x;
	/* going up */
	for (j = range, m = k; j; --j) {
		y -= 2; /* previous row */
		m -= 0x40; /* previous row */
		if ( ! handleExplodedField(TILE_ATTR(1, 0, 1, 3), FIELD_EXPL_PART_Y, FIELD_EXPL_END_Y) ) {
			break;
		}
	}
	y = bombEntry->y;
	/* going down */
	for (j = range, m = k; j; --j) {
		y += 2; /* next row */
		m += 0x40; /* next row */
		if ( ! handleExplodedField(TILE_ATTR(0, 0, 1, 3), FIELD_EXPL_PART_Y, FIELD_EXPL_END_Y) ) {
			break;
		}
	}
}


/**
 * Handle the game screen and related events.
 */
void handleGame(void) {
	if ( refreshGameScreenLow ) {
		/* update game field (only the tile indices) */
		dmaCopyVramLowBytes(gameFieldLow, WORD_OFFSET(MAP_VRAM_FG + MAP_PAGE_SIZE), sizeof(gameFieldLow));
		refreshGameScreenLow = false;
	}
	if ( refreshGameScreenHigh ) {
		/* update game field (only the tile attributes) */
		dmaCopyVramHighBytes(gameFieldHigh, WORD_OFFSET(MAP_VRAM_FG + MAP_PAGE_SIZE), sizeof(gameFieldHigh));
		refreshGameScreenHigh = false;
	}
	/* update time related variables */
	--framesUntil10Hz;
	if (framesUntil10Hz == 0) {
		framesUntil10Hz = FP10HZ;
		++counter10Hz;
		--untilSecond;
		if (untilSecond == 0) {
			untilSecond = 10;
			--gameOver;
			if (gameOver == 0) {
				winner = WINNER_DRAW;
			} else {
				/* update remaining time on screen */
				writeNumWithUnit(TILE_OFFSET(15, 1), 4, gameOver, CH_s);
				refreshGameScreenLow = true;
			}
		}
		/* update running states */
		if ( p1.running ) {
			--p1.running;
		}
		if ( p2.running ) {
			--p2.running;
		}
		/* update player 1 animation frame */
		if ( p1.moving ) {
			--p1.ttlFrame;
			if (p1.ttlFrame == 0) {
				p1.ttlFrame = PLAYER_ANIMATION;
				++p1.curFrame;
				if ((p1.curFrame - p1.firstFrame) > 2) {
					p1.curFrame = p1.firstFrame;
				}
				refreshSprites = true;
			}
		}
		/* update player 2 animation frame */
		if ( p2.moving ) {
			--p2.ttlFrame;
			if (p2.ttlFrame == 0) {
				p2.ttlFrame = PLAYER_ANIMATION;
				++p2.curFrame;
				if ((p2.curFrame - p2.firstFrame) > 2) {
					p2.curFrame = p2.firstFrame;
				}
				refreshSprites = true;
			}
		}
		/* update animated game field frames */
		for (i = 0; i < ARRAY_SIZE(fieldElemIndex); ++i) {
			ASSERT_ARY_IDX(fieldElemIndex, i);
			k = fieldElemIndex[i];
			ASSERT_ARY_IDX(ttlField, k);
			if ( ttlField[k] ) {
				--ttlField[k];
				if (ttlField[k] == 0) {
					ASSERT_ARY_IDX(aniField, k);
					--aniField[k];
					ASSERT_ARY_IDX(gameFieldLow, k);
					field = gameFieldLow + k;
					if ( aniField[k] ) {
						/* set next frame */
						ttlField[k] = EXPLOSION_ANIMATION;
						if (*field == (FIELD_BRICKED + 4)) {
							/* toggling between both bricked animation frames */
							setField(field, FIELD_BRICKED + 2);
						} else {
							nextFieldFrame(field);
						}
					} else {
						/* field is clear again */
						switch (*field) {
						case FIELD_BRICKED + 2:
						case FIELD_BRICKED + 4:
							/* roll the power-up dice */
							m = lrng();
							if ((uint8_t)m <= dropRate255) {
								/* randomize power-up type */
								switch (m & 0x0700) {
								case 0 << 8:
								case 1 << 8:
								case 2 << 8:
								case 3 << 8:
									setField(field, FIELD_PU_BOMB);
									break;
								case 4 << 8:
								case 5 << 8:
								case 6 << 8:
									setField(field, FIELD_PU_RANGE);
									break;
								default:
									setField(field, FIELD_PU_SPEED);
									break;
								}
							} else {
								clearField(field);
							}
							break;
						default:
							clearField(field);
							break;
						}
						/* reset field attributes */
						attrField = gameFieldHigh + k;
						setFieldAttr(attrField, TILE_ATTR(0, 0, 1, 3));
					}
				}
			}
		}
		/* bomb handling */
		bombChainCount = 0; /* reset list */
		/* update player 1 bomb animation frames */
		for (i = 0; i < MAX_BOMBS; ++i) {
			ASSERT_ARY_IDX(p1.bombList, i);
			if ( p1.bombList[i].ttl ) {
				--p1.bombList[i].ttl;
				if ( p1.bombList[i].ttl ) {
					/* bomb did not explode yet */
					--p1.bombList[i].ttlFrame;
					if (p1.bombList[i].ttlFrame == 0) {
						p1.bombList[i].ttlFrame = BOMB_ANIMATION;
						p1.bombList[i].curFrame ^= 1;
						field = gameFieldLow + TILE_OFFSET_1(p1.bombList[i].x, p1.bombList[i].y);
						setField(field, FIELD_BOMB_P1 + (2 * p1.bombList[i].curFrame));
					}
				} else {
					/* bomb exploded */
					++p1.bombs;
#ifdef HAS_SFX
					spcPlaySound(0);
#endif /* HAS_SFX */
					ASSERT(p1.bombs <= p1.maxBombs);
					handleExplosion(p1.range, p1.bombList + i);
				}
			}
		}
		/* update player 2 bomb animation frames */
		for (i = 0; i < MAX_BOMBS; ++i) {
			ASSERT_ARY_IDX(p2.bombList, i);
			if ( p2.bombList[i].ttl ) {
				--p2.bombList[i].ttl;
				if ( p2.bombList[i].ttl ) {
					/* bomb did not explode yet */
					--p2.bombList[i].ttlFrame;
					if (p2.bombList[i].ttlFrame == 0) {
						p2.bombList[i].ttlFrame = BOMB_ANIMATION;
						p2.bombList[i].curFrame ^= 1;
						field = gameFieldLow + TILE_OFFSET_1(p2.bombList[i].x, p2.bombList[i].y);
						setField(field, FIELD_BOMB_P2 + (2 * p2.bombList[i].curFrame));
					}
				} else {
					/* bomb exploded */
					++p2.bombs;
#ifdef HAS_SFX
					spcPlaySound(0);
#endif /* HAS_SFX */
					ASSERT(p2.bombs <= p2.maxBombs);
					handleExplosion(p2.range, p2.bombList + i);
				}
			}
		}
		/* handle chain reactions */
		for (i = 0; i < bombChainCount; ++i) {
			ASSERT_ARY_IDX(bombChain, i);
			handleExplosion(bombChain[i].range, bombChain[i].entry);
		}
	}
	/* handle user input */
	if (pad0 & KEY_START) {
#ifdef HAS_BGM
		/* lower BGM volume */
		spcSetModuleVolume(BGM_PAUSE_VOL);
#endif /* HAS_BGM */
		/* change to pause game screen */
		pausePad = &pad0;
		screen = S_PAUSE;
		/* show stop icon */
		changeClockIcon(true);
		waitForKeyReleased(0, KEY_START);
	} else if (pad1 & KEY_START) {
#ifdef HAS_BGM
		/* lower BGM volume */
		spcSetModuleVolume(BGM_PAUSE_VOL);
#endif /* HAS_BGM */
		/* change to pause game screen */
		pausePad = &pad1;
		screen = S_PAUSE;
		/* show stop icon */
		changeClockIcon(true);
		waitForKeyReleased(0, KEY_START);
	}
	handlePlayer(pad0, &p1);
	handlePlayer(pad1, &p2);
	if ( refreshSprites ) {
		updatePlayerSprites();
	}
	if (winner != WINNER_NA) {
		/* change to winner screen */
		screen = S_WINNER;
		/* draw result */
		for (i = 0; i < 64; ++i) {
			gameFieldLow[i] = FIELD_EMPTY;
		}
		switch (winner) {
		case WINNER_P1:
			gameFieldLow[TILE_OFFSET(13, 0)] = CH_P;
			gameFieldLow[TILE_OFFSET(14, 0)] = CH_1;
			oamSetVisible(P2_NR, OBJ_HIDE);
			break;
		case WINNER_P2:
			gameFieldLow[TILE_OFFSET(13, 0)] = CH_P;
			gameFieldLow[TILE_OFFSET(14, 0)] = CH_2;
			oamSetVisible(P1_NR, OBJ_HIDE);
			break;
		case WINNER_DRAW:
			gameFieldLow[TILE_OFFSET(13, 0)] = CH_P;
			gameFieldLow[TILE_OFFSET(14, 0)] = CH_1;
			gameFieldLow[TILE_OFFSET(13, 1)] = CH_P;
			gameFieldLow[TILE_OFFSET(14, 1)] = CH_2;
			break;
		default:
			break;
		}
		setField(gameFieldLow + TILE_OFFSET(15, 0), FIELD_TROPHY);
		WaitForVBlank();
		dmaCopyVramLowBytes(gameFieldLow, WORD_OFFSET(MAP_VRAM_FG + MAP_PAGE_SIZE), sizeof(gameFieldLow));
	}
}


/**
 * Handle the pause game screen and related events.
 */
void handlePause(void) {
	if (pausePad == &pad0 && (pad0 & KEY_SELECT)) {
#ifdef HAS_BGM
		/* reset BGM volume */
		spcSetModuleVolume(BGM_NORMAL_VOL);
#endif /* HAS_BGM */
		/* change to options screen */
		bgSlideOut(FG_NR, BG_NR, true);
		screen = S_OPTIONS;
		option = O_TIME;
		/* replace hidden second page */
		dmaCopyVram(bg1Map, WORD_OFFSET(MAP_VRAM_BG + MAP_PAGE_SIZE), MAP_PAGE_SIZE);
		dmaCopyVram(optionsMap, WORD_OFFSET(MAP_VRAM_FG + MAP_PAGE_SIZE), MAP_PAGE_SIZE);
		oamSetVisible(P1_NR, OBJ_HIDE);
		oamSetVisible(P2_NR, OBJ_HIDE);
		updateOptionsScreen();
		bgSlideIn(FG_NR, BG_NR, false);
	} else if (*pausePad & KEY_START) {
#ifdef HAS_BGM
		/* reset BGM volume */
		spcSetModuleVolume(BGM_NORMAL_VOL);
#endif /* HAS_BGM */
		/* change to game screen */
		screen = S_GAME;
		/* show stop icon */
		changeClockIcon(false);
		waitForKeyReleased(0, KEY_START);
	}
}


/**
 * Handle the show winner screen and related events.
 */
void handleWinner(void) {
	if (pad0 & (uint16_t)(KEY_START | KEY_SELECT)) {
		/* change to options screen */
		bgSlideOut(FG_NR, BG_NR, true);
		screen = S_OPTIONS;
		option = O_TIME;
		/* replace hidden second page */
		dmaCopyVram(bg1Map, WORD_OFFSET(MAP_VRAM_BG + MAP_PAGE_SIZE), MAP_PAGE_SIZE);
		dmaCopyVram(optionsMap, WORD_OFFSET(MAP_VRAM_FG + MAP_PAGE_SIZE), MAP_PAGE_SIZE);
		oamSetVisible(P1_NR, OBJ_HIDE);
		oamSetVisible(P2_NR, OBJ_HIDE);
		updateOptionsScreen();
		bgSlideIn(FG_NR, BG_NR, false);
	}
}


/**
 * Main entry point.
 */
int main(void) {
	/* initialize sound engine (slow) */
#if defined(HAS_BGM) || defined(HAS_SFX)
	spcBoot();
#endif /* HAS_BGM or HAS_SFX */

	/* initialize SNES */
	consoleInit();

#ifdef HAS_BGM
	/* set soundbank available in bgm1.asm in reverse order */
	spcSetBank(&SOUNDBANK__1);
	spcSetBank(&SOUNDBANK__0);
#endif /* HAS_BGM */

#ifdef HAS_SFX
	/* allocate sound ram (14x 256-byte blocks) */
	spcAllocateSoundRegion(14);
#endif /* HAS_SFX */

#ifdef HAS_BGM
	/* load the background music using the ID defined in `bgm1.h` */
	spcLoad(MOD_BGM1);
#endif /* HAS_BGM */
#ifdef HAS_SFX
	/* load sound effect */
	spcSetSoundEntry(13, 7, 5, (sfx1End - sfx1), sfx1, sfx1Sample);
#endif /* HAS_SFX */

	/* SNES background layer map for the background with two pages of 32x32 tiles */
	bgSetMapPtr(BG_NR, WORD_OFFSET(MAP_VRAM_BG), SC_64x32);
	/* SNES background layer map for the foreground with two pages of 32x32 tiles */
	bgSetMapPtr(FG_NR, WORD_OFFSET(MAP_VRAM_FG), SC_64x32);

	/* copy foreground/background tiles and palettes to VRAM */
	bgInitTileSet(BG_NR, bg1Tiles, bg1Pal, 1, (bg1TilesEnd - bg1Tiles), (bg1PalEnd - bg1Pal), BG_16COLORS, WORD_OFFSET(CHR_VRAM_BG1));
	bgInitTileSet(FG_NR, fg1Tiles, fg1Pal, 2, (fg1TilesEnd - fg1Tiles), (fg1PalEnd - fg1Pal), BG_16COLORS, WORD_OFFSET(CHR_VRAM_FG1));
	bgInitTileSet(FG_NR, fg2Tiles, fg2Pal, 3, (fg2TilesEnd - fg2Tiles), (fg2PalEnd - fg2Pal), BG_16COLORS, WORD_OFFSET(CHR_VRAM_FG2));
	bgSetGfxPtr(FG_NR, WORD_OFFSET(CHR_VRAM_FG1));

	/* copy sprite tiles and palettes to VRAM */
	oamInitGfxSet(p12Tiles, (p12TilesEnd - p12Tiles), p12Pal, (p12PalEnd - p12Pal), 4, WORD_OFFSET(CHR_VRAM_P1), OBJ_SIZE16_L32);

	/* set sprite parameters */
	oamSet(P1_NR, 0, 0, 3, 0, 0, playerTileMap[ACT_DOWN], 4);
	oamSet(P2_NR, 0, 0, 3, 0, 0, playerTileMap[ACT_DOWN], 5);
	oamSetEx(P1_NR, OBJ_SMALL, OBJ_HIDE);
	oamSetEx(P2_NR, OBJ_SMALL, OBJ_HIDE);

	/* disable screen and wait for VBlank to allow VRAM updates */
	setBrightness(0);
	WaitForVBlank();

	/* load initial maps into VRAM */
	dmaFillVramWord(0x0401, WORD_OFFSET(MAP_VRAM_BG), MAP_PAGE_SIZE); /* first page */
	dmaCopyVram(bg1Map, WORD_OFFSET(MAP_VRAM_BG + MAP_PAGE_SIZE), MAP_PAGE_SIZE); /* second page */
	dmaFillVramWord(0x2800, WORD_OFFSET(MAP_VRAM_FG), MAP_PAGE_SIZE); /* first page */
	dmaCopyVram(fg1Map, WORD_OFFSET(MAP_VRAM_FG + MAP_PAGE_SIZE), MAP_PAGE_SIZE); /* second page */

	/* set 16 color mode and disable background 2 */
	setMode(BG_MODE1, BG3_MODE1_PRORITY_HIGH);
	bgSetDisable(2);
	bgSetDisable(3);

	/* enable screen */
	setScreenOn();

	/* start BGM with little volume */
#ifdef HAS_BGM
	spcSetModuleVolume(BGM_NORMAL_VOL);
	spcPlay(0);
#endif /* HAS_BGM */

	/* show title screen */
	screen = S_TITLE;
	bgSlideIn(FG_NR, BG_NR, false);
	/* initialize configuration */
	maxTime = DEF_MAX_TIME;
	dropRate = DEF_DROP_RATE;
	maxBombs = DEF_MAX_BOMBS;
	maxRange = DEF_MAX_RANGE;
	for (;;) {
		pad0 = padsCurrent(0);
		pad1 = padsCurrent(1);
		ASSERT_ARY_IDX(screenHandler, screen);
		screenHandler[screen]();
		WaitForVBlank();
	}
	return 0;
}
