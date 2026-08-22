#ifndef KBHE_TEST_ASSERTIONS_ACTIVE_H_
#define KBHE_TEST_ASSERTIONS_ACTIVE_H_

/* CMake's Release flags normally define NDEBUG, which silently removes both
 * the checks and any function calls embedded in assert(...). This header is
 * force-included before every host-test translation unit so assertions remain
 * executable regardless of the selected build configuration. New tests should
 * still prefer an always-on CHECK macro when an assertion has side effects. */
#ifdef NDEBUG
#undef NDEBUG
#endif

#endif /* KBHE_TEST_ASSERTIONS_ACTIVE_H_ */
