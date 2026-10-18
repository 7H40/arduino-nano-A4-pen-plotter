/*
 * G-code line parser
 * Reference: API/protocol&&RULES.v2.md (v2.9.4).
 * Даня Нигерия ; danyanigeria@proton.me
 * MISRA C:2012: PVS-Studio MISRA add-on reports 0 findings (make pvs).
 * make pvs       -> 0 предупреждений MISRA C:2012
 * make pvs-all   -> 0 предупреждений GA + MISRA C:2012
 *| Метрика                               | Ныне                           |
 *| ---                                   | ---                            |
 *| flash, объект парсера                 | 2457 Б (+77 Б)                 |
 *| `.data` в линкованном тесте           | 40 Б (−70 Б)                   |
 *| теги ошибок в SRAM                    | 0 Б (`.progmem`)               |
 *| кадр стека (макс. по `-fstack-usage`) | 26 Б на функцию, цепочка ≈70 Б |
 *
 */

#ifndef GCODE_PARSER_H
#define GCODE_PARSER_H

#include <stddef.h>
#include <stdint.h>

#define GCODE_MAX_LINE_BYTES ((size_t)63U)

#define GCODE_TAG_CAPACITY ((size_t)16U)

#define GCODE_HAS_X	 ((uint8_t)0x01U)
#define GCODE_HAS_Y	 ((uint8_t)0x02U)
#define GCODE_HAS_F	 ((uint8_t)0x04U)
#define GCODE_HAS_P	 ((uint8_t)0x08U)
#define GCODE_HAS_S	 ((uint8_t)0x10U)
#define GCODE_HAS_XY ((uint8_t)(GCODE_HAS_X | GCODE_HAS_Y))

typedef enum {
		GCODE_CMD_EMPTY = 0, /* Blank line: keepalive, answered with "ok". */
		GCODE_CMD_STATUS,	 /* ?  */
		GCODE_CMD_SETTINGS,	 /* $$ */
		GCODE_CMD_G0,
		GCODE_CMD_G1,
		GCODE_CMD_G28,
		GCODE_CMD_G4,
		GCODE_CMD_G92,
		GCODE_CMD_SET_FEED, /* F<int> */
		GCODE_CMD_M3,
		GCODE_CMD_M5,
		GCODE_CMD_M17,
		GCODE_CMD_M18,
		GCODE_CMD_M201,
		GCODE_CMD_M112,
		GCODE_CMD_M114,
		GCODE_CMD_M115,
		GCODE_CMD_M999
} gcode_command_id_t;

typedef struct {
		gcode_command_id_t id;
		uint8_t
			present;  /* GCODE_HAS_* mask.  G28 uses X/Y as axis selectors. */
		int32_t x;	  /* X coordinate, G92 value or M201 X acceleration. */
		int32_t y;	  /* Y coordinate, G92 value or M201 Y acceleration. */
		int32_t feed; /* F word, steps/second. */
		int32_t p;	  /* G4 dwell, milliseconds. */
		int32_t s;	  /* M3 PWM override, 0..255. */
} gcode_command_t;

typedef struct {
		int32_t travel_x; /* Maximum X coordinate in steps, 0..999999. */
		int32_t travel_y; /* Maximum Y coordinate in steps, 0..999999. */
		int32_t fmax;	  /* Maximum feed in steps/second, >= 1. */
} gcode_limits_t;

typedef enum {
		GCODE_CLASS_INVALID = 0,
		GCODE_CLASS_KEEPALIVE,
		GCODE_CLASS_EXEC,
		GCODE_CLASS_SET,
		GCODE_CLASS_QUERY,
		GCODE_CLASS_ASYNC,
		GCODE_CLASS_REALTIME_LINE
} gcode_command_class_t;

typedef enum {
		GCODE_PARSE_OK = 0,
		GCODE_PARSE_BAD_PARAM,
		GCODE_PARSE_UNKNOWN_CMD,
		GCODE_PARSE_SOFT_LIMIT,
		GCODE_PARSE_LINE_TOO_LONG,
		GCODE_PARSE_RANGE,
		GCODE_PARSE_INTERNAL
} gcode_parse_result_t;

gcode_parse_result_t
gcode_parse_line (const char *line, size_t length, gcode_command_t *out);

gcode_command_class_t
gcode_command_class (gcode_command_id_t id);

gcode_parse_result_t
gcode_validate_ranges (const gcode_command_t *command,
					   const gcode_limits_t	 *limits);

uint8_t
gcode_result_error_code (gcode_parse_result_t result);

void
gcode_result_error_tag (gcode_parse_result_t result, char *buffer,
						size_t capacity);

#endif 
