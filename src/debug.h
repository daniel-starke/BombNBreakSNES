/**
* @file debug.h
* @author Daniel Starke
* @copyright Copyright 2023 Daniel Starke
* @date 2023-07-16
* @version 2023-07-21
*/
#ifndef _DEBUG_H_
#define _DEBUG_H_

#include <stddef.h>


/** Found in `debug.asm`. */
extern const char * debugMessage;


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
 * @param[in] x - debug message to set
 */
#define DEBUG_MSG(x) \
	{debugMessage = PP_STR(__FILE__) ":" PP_STR(__LINE__) ":" x; debugBreak(); }


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
 * Calls the break command.
 */
void debugBreak();


#endif /* _DEBUG_H_ */
