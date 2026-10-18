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

#include "gcode_parser.h"
#include <stdbool.h>
#include <string.h>

#if defined(__AVR__)
#include <avr/pgmspace.h>
#define GCODE_FLASH_ATTR PROGMEM
#else
#define GCODE_FLASH_ATTR
#endif

#define GCODE_DECIMAL_BASE		  ((uint32_t)10U)
#define GCODE_DIGIT_ZERO		  ((uint32_t)48U) /* ASCII '0' */
#define GCODE_INT32_MAX			  ((uint32_t)2147483647U)
#define GCODE_INT32_MIN_MAGNITUDE ((uint32_t)2147483648U)

typedef struct {
		const char *text;
		size_t		length;
} gcode_token_t;

static const char GCODE_TAG_OK[] GCODE_FLASH_ATTR			 = "OK";
static const char GCODE_TAG_BAD_PARAM[] GCODE_FLASH_ATTR	 = "BAD_PARAM";
static const char GCODE_TAG_UNKNOWN_CMD[] GCODE_FLASH_ATTR	 = "UNKNOWN_CMD";
static const char GCODE_TAG_SOFT_LIMIT[] GCODE_FLASH_ATTR	 = "SOFT_LIMIT";
static const char GCODE_TAG_LINE_TOO_LONG[] GCODE_FLASH_ATTR = "LINE_TOO_LONG";
static const char GCODE_TAG_RANGE[] GCODE_FLASH_ATTR		 = "RANGE";
static const char GCODE_TAG_INTERNAL[] GCODE_FLASH_ATTR		 = "INTERNAL";

static void
copy_flash_string (const char *source, char *destination, size_t capacity)
{
		size_t length;

#if defined(__AVR__)
		length = strlen_P (source);
		if (length >= capacity)
				{
						length = capacity - (size_t)1U;
				}
		(void)memcpy_P (destination, source, length);
#else
		length = strlen (source);
		if (length >= capacity)
				{
						length = capacity - (size_t)1U;
				}
		(void)memcpy (destination, source, length);
#endif

		destination[length] = '\0';
}

static void
clear_command (gcode_command_t *command)
{
		command->id		 = GCODE_CMD_EMPTY;
		command->present = (uint8_t)0U;
		command->x		 = (int32_t)0;
		command->y		 = (int32_t)0;
		command->feed	 = (int32_t)0;
		command->p		 = (int32_t)0;
		command->s		 = (int32_t)0;
}

static char
ascii_upper (char value)
{
		char result = value;

		if ((value >= 'a') && (value <= 'z'))
				{
						int temp  = (int)value - (int)'a';
						temp	 += (int)'A';
						result	  = (char)temp;
				}
		else
				{
				}

		return result;
}

static bool
is_delimiter (char value)
{
		return (value == ' ') || (value == '\t') || (value == '\r') ||
			   (value == '\n');
}

static bool
next_token (const char *line, size_t length, size_t *position,
			gcode_token_t *token)
{ // xref :: is_delimiter
		bool found = (bool)0;

		while ((*position < length) && is_delimiter (line[*position]))
				{
						++(*position);
				}

		if (*position < length)
				{
						const size_t start = *position;

						while ((*position < length) &&
							   (!is_delimiter (line[*position])))
								{
										++(*position);
								}

						token->text	  = &line[start];
						token->length = *position - start;
						found		  = (bool)1;
				}
		else
				{
						token->text	  = NULL;
						token->length = (size_t)0U;
						found		  = (bool)0;
				}

		return found;
}

static gcode_parse_result_t
parse_decimal (const gcode_token_t *token, size_t offset, int32_t *value)
{
		gcode_parse_result_t result = GCODE_PARSE_OK;

		if ((token == NULL) || (value == NULL) || (offset >= token->length))
				{
						result = GCODE_PARSE_BAD_PARAM;
				}
		else
				{
						const bool negative	 = (token->text[offset] == '-');
						uint32_t   magnitude = (uint32_t)0U;
						uint32_t   limit	 = GCODE_INT32_MAX;
						size_t	   index	 = offset;

						if (negative)
								{
										limit = GCODE_INT32_MIN_MAGNITUDE;
										++index;
								}
						else if (token->text[offset] == '+')
								{
										++index;
								}
						else
								{
								}

						if (index >= token->length)
								{
										result = GCODE_PARSE_BAD_PARAM;
								}
						else
								{
										while ((index < token->length) &&
											   (result == GCODE_PARSE_OK))
												{
														const char current =
															token->text[index];

														if ((current < '0') ||
															(current > '9'))
																{
																		result =
																			GCODE_PARSE_BAD_PARAM;
																}
														else
																{
																		const uint32_t digit =
																			(uint32_t)
																				current -
																			GCODE_DIGIT_ZERO;

																		if (magnitude >
																			((limit -
																			  digit) /
																			 GCODE_DECIMAL_BASE))
																				{
																						result =
																							GCODE_PARSE_BAD_PARAM;
																				}
																		else
																				{
																						magnitude =
																							(magnitude *
																							 GCODE_DECIMAL_BASE) +
																							digit;
																				}
																}

														++index;
												}

										if (result == GCODE_PARSE_OK)
												{
														if (negative)
																{
																		if (magnitude ==
																			GCODE_INT32_MIN_MAGNITUDE)
																				{
																						*value =
																							INT32_MIN;
																				}
																		else
																				{
																						*value =
																							(int32_t)((int32_t)0 -
																									  (int32_t)
																										  magnitude);
																				}
																}
														else
																{
																		*value =
																			(int32_t)
																				magnitude;
																}
												}
										else
												{
												}
								}
				}

		return result;
}

static gcode_parse_result_t
parse_word (const gcode_token_t *token, char expected_prefix, int32_t *value)
{ // xref :: ascii_upper,parse_decimal
		gcode_parse_result_t result;

		if ((token == NULL) || (token->length < (size_t)2U) ||
			(ascii_upper (token->text[0]) != expected_prefix))
				{
						result = GCODE_PARSE_BAD_PARAM;
				}
		else
				{
						result = parse_decimal (token, (size_t)1U, value);
				}

		return result;
}

static gcode_parse_result_t
set_value (gcode_command_t *command, uint8_t bit, const gcode_token_t *token,
		   char prefix, int32_t *destination)
{ // xref :: parse_word
		gcode_parse_result_t result;

		if ((command->present & bit) != (uint8_t)0U)
				{
						result = GCODE_PARSE_UNKNOWN_CMD;
				}
		else
				{
						result = parse_word (token, prefix, destination);
						if (result == GCODE_PARSE_OK)
								{
										command->present =
											(uint8_t)(command->present | bit);
								}
						else
								{
								}
				}

		return result;
}

static gcode_parse_result_t
parse_g28_argument (gcode_command_t *command, const gcode_token_t *token,
					char prefix)
{
		gcode_parse_result_t result;
		uint8_t				 bit = (uint8_t)0U;

		if (prefix == 'X')
				{
						bit = GCODE_HAS_X;
				}
		else if (prefix == 'Y')
				{
						bit = GCODE_HAS_Y;
				}
		else
				{
				}

		if (bit == (uint8_t)0U)
				{
						result = GCODE_PARSE_UNKNOWN_CMD;
				}
		else if (token->length != (size_t)1U)
				{
						result = GCODE_PARSE_BAD_PARAM;
				}
		else if ((command->present & bit) != (uint8_t)0U)
				{
						result = GCODE_PARSE_UNKNOWN_CMD;
				}
		else
				{
						command->present = (uint8_t)(command->present | bit);
						result			 = GCODE_PARSE_OK;
				}

		return result;
}

static gcode_parse_result_t
parse_xy_argument (gcode_command_t *command, const gcode_token_t *token,
				   char prefix)
{ // xref :: set_value
		gcode_parse_result_t result;

		if (prefix == 'X')
				{
						result = set_value (command, GCODE_HAS_X, token, 'X',
											&command->x);
				}
		else if (prefix == 'Y')
				{
						result = set_value (command, GCODE_HAS_Y, token, 'Y',
											&command->y);
				}
		else
				{
						result = GCODE_PARSE_UNKNOWN_CMD;
				}

		return result;
}

static gcode_parse_result_t
parse_single_argument (gcode_command_t *command, const gcode_token_t *token,
					   char prefix, char expected_prefix, uint8_t bit,
					   int32_t *destination)
{ // xref :: set_value
		gcode_parse_result_t result;

		if (prefix == expected_prefix)
				{
						result = set_value (command, bit, token,
											expected_prefix, destination);
				}
		else
				{
						result = GCODE_PARSE_UNKNOWN_CMD;
				}

		return result;
}

static gcode_parse_result_t
parse_argument (gcode_command_t *command, const gcode_token_t *token)
{ // xref :: ascii_upper,parse_g28_argument,set_value,parse_xy_argument,parse_single_argument
		const char			 prefix = ascii_upper (token->text[0]);
		gcode_parse_result_t result;

		if (command->id == GCODE_CMD_G28)
				{
						result = parse_g28_argument (command, token, prefix);
				}
		else
				{
						switch (command->id)
								{
								case GCODE_CMD_G0:
								case GCODE_CMD_G1:
										if (prefix == 'F')
												{
														result = set_value (
															command,
															GCODE_HAS_F, token,
															'F',
															&command->feed);
												}
										else
												{
														result =
															parse_xy_argument (
																command, token,
																prefix);
												}
										break;

								case GCODE_CMD_G92:
								case GCODE_CMD_M201:
										result = parse_xy_argument (
											command, token, prefix);
										break;

								case GCODE_CMD_G4:
										result = parse_single_argument (
											command, token, prefix, 'P',
											GCODE_HAS_P, &command->p);
										break;

								case GCODE_CMD_M3:
										result = parse_single_argument (
											command, token, prefix, 'S',
											GCODE_HAS_S, &command->s);
										break;

								default:
										// NOARGs
										result = GCODE_PARSE_UNKNOWN_CMD;
										break;
								}
				}

		return result;
}

static gcode_parse_result_t
select_command (gcode_command_t *command, char prefix, int32_t code)
{
		gcode_parse_result_t result = GCODE_PARSE_OK;

		if (prefix == 'G')
				{
						switch (code)
								{
								case (int32_t)0:
										command->id = GCODE_CMD_G0;
										break;
								case (int32_t)1:
										command->id = GCODE_CMD_G1;
										break;
								case (int32_t)4:
										command->id = GCODE_CMD_G4;
										break;
								case (int32_t)28:
										command->id = GCODE_CMD_G28;
										break;
								case (int32_t)92:
										command->id = GCODE_CMD_G92;
										break;
								default:
										result = GCODE_PARSE_UNKNOWN_CMD;
										break;
								}
				}
		else
				{
						switch (code)
								{
								case (int32_t)3:
										command->id = GCODE_CMD_M3;
										break;
								case (int32_t)5:
										command->id = GCODE_CMD_M5;
										break;
								case (int32_t)17:
										command->id = GCODE_CMD_M17;
										break;
								case (int32_t)18:
										command->id = GCODE_CMD_M18;
										break;
								case (int32_t)112:
										command->id = GCODE_CMD_M112;
										break;
								case (int32_t)114:
										command->id = GCODE_CMD_M114;
										break;
								case (int32_t)115:
										command->id = GCODE_CMD_M115;
										break;
								case (int32_t)201:
										command->id = GCODE_CMD_M201;
										break;
								case (int32_t)999:
										command->id = GCODE_CMD_M999;
										break;
								default:
										result = GCODE_PARSE_UNKNOWN_CMD;
										break;
								}
				}

		return result;
}

static gcode_parse_result_t
assign_command (gcode_command_t *command, const gcode_token_t *first)
{ // xref :: ascii_upper,parse_word,select_command
		gcode_parse_result_t result;
		int32_t				 code = (int32_t)0;

		if ((first->length == (size_t)1U) && (first->text[0] == '?'))
				{
						command->id = GCODE_CMD_STATUS;
						result		= GCODE_PARSE_OK;
				}
		else if ((first->length == (size_t)2U) && (first->text[0] == '$') &&
				 (first->text[1] == '$'))
				{
						command->id = GCODE_CMD_SETTINGS;
						result		= GCODE_PARSE_OK;
				}
		else
				{
						const char prefix = ascii_upper (first->text[0]);

						if (prefix == 'F')
								{
										command->id = GCODE_CMD_SET_FEED;
										result		= parse_word (first, 'F',
																  &command->feed);
										if (result == GCODE_PARSE_OK)
												{
														command->present =
															(uint8_t)(command
																		  ->present |
																	  GCODE_HAS_F);
												}
										else
												{
												}
								}
						else if ((prefix == 'G') || (prefix == 'M'))
								{
										result =
											parse_word (first, prefix, &code);
										if (result == GCODE_PARSE_OK)
												{
														result =
															select_command (
																command, prefix,
																code);
												}
										else
												{
												}
								}
						else
								{
										result = GCODE_PARSE_UNKNOWN_CMD;
								}
				}

		return result;
}

static gcode_parse_result_t
check_line (const char *line, size_t length)
{
		gcode_parse_result_t result;
		size_t				 index;

		if (line == NULL)
				{
						result = GCODE_PARSE_INTERNAL;
				}
		else if (length > GCODE_MAX_LINE_BYTES)
				{
						result = GCODE_PARSE_LINE_TOO_LONG;
				}
		else
				{
						result = GCODE_PARSE_OK;

						for (index = (size_t)0U; index < length; ++index)
								{
										if ((line[index] == ';') ||
											(line[index] == '('))
												{
														result =
															GCODE_PARSE_UNKNOWN_CMD;
														break;
												}
										else
												{
												}
								}
				}

		return result;
}

static gcode_parse_result_t
check_words (const gcode_command_t *command)
{
		gcode_parse_result_t result = GCODE_PARSE_OK;

		switch (command->id)
				{
				case GCODE_CMD_G0:
				case GCODE_CMD_G1:
				case GCODE_CMD_G92:
				case GCODE_CMD_M201:
						if ((command->present & GCODE_HAS_XY) != GCODE_HAS_XY)
								{
										result = GCODE_PARSE_BAD_PARAM;
								}
						else
								{
								}
						break;

				case GCODE_CMD_G4:
						if ((command->present & GCODE_HAS_P) == (uint8_t)0U)
								{
										result = GCODE_PARSE_BAD_PARAM;
								}
						else
								{
								}
						break;

				case GCODE_CMD_G28:
						/* G28 X Y is explicitly forbidden by section 6. */
						if ((command->present & GCODE_HAS_XY) == GCODE_HAS_XY)
								{
										result = GCODE_PARSE_UNKNOWN_CMD;
								}
						else
								{
								}
						break;

				default:
						break;
				}

		return result;
}

static gcode_parse_result_t
parse_tokens (const char *line, size_t length, gcode_command_t *command)
{ // xref :: next_token,assign_command,parse_argument
		size_t				 position = (size_t)0U;
		gcode_token_t		 token;
		gcode_parse_result_t result;
		bool have_token = next_token (line, length, &position, &token);

		if (!have_token)
				{
						result = GCODE_PARSE_OK;
				}
		else
				{
						result = assign_command (command, &token);
						have_token =
							next_token (line, length, &position, &token);

						while ((result == GCODE_PARSE_OK) && have_token)
								{
										result =
											parse_argument (command, &token);
										have_token = next_token (
											line, length, &position, &token);
								}
				}

		return result;
}

gcode_parse_result_t
gcode_parse_line (const char *line, size_t length, gcode_command_t *out)
{ // xref :: clear_command,check_line,parse_tokens,check_words
		gcode_parse_result_t result = GCODE_PARSE_INTERNAL;

		if (out == NULL)
				{
						/* No output storage: nothing can be produced. */
				}
		else
				{
						clear_command (out);
						result = check_line (line, length);

						if (result == GCODE_PARSE_OK)
								{
										result =
											parse_tokens (line, length, out);
								}
						else
								{
								}

						if (result == GCODE_PARSE_OK)
								{
										result = check_words (out);
								}
						else
								{
								}

						if (result != GCODE_PARSE_OK)
								{
										clear_command (out);
								}
						else
								{
								}
				}

		return result;
}

gcode_command_class_t
gcode_command_class (gcode_command_id_t id)
{
		gcode_command_class_t result;

		switch (id)
				{
				case GCODE_CMD_EMPTY:
						result = GCODE_CLASS_KEEPALIVE;
						break;

				case GCODE_CMD_G0:
				case GCODE_CMD_G1:
				case GCODE_CMD_G28:
				case GCODE_CMD_G4:
				case GCODE_CMD_M3:
				case GCODE_CMD_M5:
				case GCODE_CMD_M17:
				case GCODE_CMD_M18:
						result = GCODE_CLASS_EXEC;
						break;

				case GCODE_CMD_G92:
				case GCODE_CMD_SET_FEED:
				case GCODE_CMD_M201:
				case GCODE_CMD_M999:
						result = GCODE_CLASS_SET;
						break;

				case GCODE_CMD_SETTINGS:
				case GCODE_CMD_M114:
				case GCODE_CMD_M115:
						result = GCODE_CLASS_QUERY;
						break;

				case GCODE_CMD_STATUS:
						result = GCODE_CLASS_ASYNC;
						break;

				case GCODE_CMD_M112:
						result = GCODE_CLASS_REALTIME_LINE;
						break;

				default:
						result = GCODE_CLASS_INVALID;
						break;
				}

		return result;
}

gcode_parse_result_t
gcode_validate_ranges (const gcode_command_t *command,
					   const gcode_limits_t	 *limits)
{ // xref :: gcode_command_class
		gcode_parse_result_t result = GCODE_PARSE_OK;

		if ((command == NULL) || (limits == NULL))
				{
						result = GCODE_PARSE_INTERNAL;
				}
		else if ((gcode_command_class (command->id) == GCODE_CLASS_INVALID) ||
				 (limits->travel_x < (int32_t)0) ||
				 (limits->travel_y < (int32_t)0) ||
				 (limits->travel_x > (int32_t)999999) ||
				 (limits->travel_y > (int32_t)999999) ||
				 (limits->fmax < (int32_t)1))
				{
						result = GCODE_PARSE_INTERNAL;
				}
		else
				{
						switch (command->id)
								{
								case GCODE_CMD_G0:
								case GCODE_CMD_G1:
								case GCODE_CMD_G92:
										if ((command->x < (int32_t)0) ||
											(command->x > limits->travel_x) ||
											(command->y < (int32_t)0) ||
											(command->y > limits->travel_y))
												{
														result =
															GCODE_PARSE_SOFT_LIMIT;
												}
										else if ((command->id ==
												  GCODE_CMD_G1) &&
												 ((command->present &
												   GCODE_HAS_F) !=
												  (uint8_t)0U) &&
												 ((command->feed <
												   (int32_t)1) ||
												  (command->feed >
												   limits->fmax)))
												{
														result =
															GCODE_PARSE_RANGE;
												}
										else
												{
												}
										break;

								case GCODE_CMD_SET_FEED:
										if ((command->feed < (int32_t)1) ||
											(command->feed > limits->fmax))
												{
														result =
															GCODE_PARSE_RANGE;
												}
										else
												{
												}
										break;

								case GCODE_CMD_G4:
										if ((command->p < (int32_t)0) ||
											(command->p > (int32_t)600000))
												{
														result =
															GCODE_PARSE_RANGE;
												}
										else
												{
												}
										break;

								case GCODE_CMD_M3:
										if (((command->present & GCODE_HAS_S) !=
											 (uint8_t)0U) &&
											((command->s < (int32_t)0) ||
											 (command->s > (int32_t)255)))
												{
														result =
															GCODE_PARSE_RANGE;
												}
										else
												{
												}
										break;

								case GCODE_CMD_M201:
										if ((command->x < (int32_t)1) ||
											(command->x > (int32_t)65535) ||
											(command->y < (int32_t)1) ||
											(command->y > (int32_t)65535))
												{
														result =
															GCODE_PARSE_RANGE;
												}
										else
												{
												}
										break;

								default:

										break;
								}
				}

		return result;
}

uint8_t
gcode_result_error_code (gcode_parse_result_t result)
{
		uint8_t code;

		switch (result)
				{
				case GCODE_PARSE_OK:
						code = (uint8_t)0x00U;
						break;
				case GCODE_PARSE_BAD_PARAM:
						code = (uint8_t)0xF1U;
						break;
				case GCODE_PARSE_UNKNOWN_CMD:
						code = (uint8_t)0xF2U;
						break;
				case GCODE_PARSE_SOFT_LIMIT:
						code = (uint8_t)0xF3U;
						break;
				case GCODE_PARSE_LINE_TOO_LONG:
						code = (uint8_t)0xF6U;
						break;
				case GCODE_PARSE_RANGE:
						code = (uint8_t)0xFEU;
						break;
				case GCODE_PARSE_INTERNAL:
						code = (uint8_t)0xF0U;
						break;
				default:
						code = (uint8_t)0xF0U;
						break;
				}

		return code;
}

void
gcode_result_error_tag (gcode_parse_result_t result, char *buffer,
						size_t capacity)
{ // xref :: copy_flash_string
		const char *tag;

		if ((buffer == NULL) || (capacity == (size_t)0U))
				{
				}
		else
				{
						switch (result)
								{
								case GCODE_PARSE_OK:
										tag = GCODE_TAG_OK;
										break;
								case GCODE_PARSE_BAD_PARAM:
										tag = GCODE_TAG_BAD_PARAM;
										break;
								case GCODE_PARSE_UNKNOWN_CMD:
										tag = GCODE_TAG_UNKNOWN_CMD;
										break;
								case GCODE_PARSE_SOFT_LIMIT:
										tag = GCODE_TAG_SOFT_LIMIT;
										break;
								case GCODE_PARSE_LINE_TOO_LONG:
										tag = GCODE_TAG_LINE_TOO_LONG;
										break;
								case GCODE_PARSE_RANGE:
										tag = GCODE_TAG_RANGE;
										break;
								default:
										tag = GCODE_TAG_INTERNAL;
										break;
								}

						copy_flash_string (tag, buffer, capacity);
				}
}