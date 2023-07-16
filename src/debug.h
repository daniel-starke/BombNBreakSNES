/**
* @file debug.h
* @author Daniel Starke
* @copyright Copyright 2023 Daniel Starke
* @date 2023-07-16
* @version 2023-07-16
*/
#ifndef _DEBUG_H_
#define _DEBUG_H_

#include <stddef.h>


/** Concatenates two tokens. */
#define PP_CAT(x, y) PP_CAT_HELPER1(x, y)
#define PP_CAT_HELPER1(x, y) PP_CAT_HELPER2(x, y)
#define PP_CAT_HELPER2(x, y) x ## y


/** Returns a char[] literal of the given argument. */
#define PP_STR(x) PP_STR_HELPER(x)
#define PP_STR_HELPER(x) #x


/**
 * Calls the debugger with the given message.
 * 
 * @param[in] x - message to pass
 */
#define DEBUG_MSG(x) \
	debugBreak(PP_STR(__FILE__) ":" PP_STR(__LINE__) ":" x)


/**
 * @def ASSERT(x)
 * 
 * @param x - condition to assert
 */
/**
 * @def ASSERT_ARY_IDX(ary, idx)
 * 
 * @param ary - array to check (size needs to be known at compile time)
 * @param idx - array index to test
 */
#ifndef NDEBUG
#define ASSERT(x) \
	if ( ! (x) ) { DEBUG_MSG(PP_STR(x)); }
#define ASSERT_ARY_IDX(ary, idx) \
	if (idx >= (sizeof(ary) / sizeof(*(ary)))) { DEBUG_MSG("out of bounds"); }
#define ASSERT_ARY_PTR(ary, ptr) \
	ASSERT_ARY_IDX((ary), ((ptr) - (ary)))
#else /* NDEBUG */
#define ASSERT(x)
#define ASSERT_ARY_IDX(ary, idx)
#define ASSERT_ARY_PTR(ary, ptr)
#endif /* NDEBUG */


/**
 * Calls the break command. A message can be passed
 * to identify the source/cause for this command.
 * 
 * @param[in] msg - associated message
 */
void debugBreak(const char * msg);


#endif /* _DEBUG_H_ */
