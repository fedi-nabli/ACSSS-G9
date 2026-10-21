/* ===================================================================================================
 * tests/app/link/link_tests.c
 *
 * Tests for app/link from C. The Led_Command layout is checked at compile
 * time by the header; this checks it survives the mailbox it travels in.
 *
 * Authors: Fedi Nabli
 * Date: 20 Oct 2026
 * Last Modified: 20 Oct 2026
 * ===================================================================================================
 */

#include <mts_test.h>
#include <app/link/led_command.h>
#include <utils/mailbox/mailbox.h>

#include <string.h>

MTS_TEST(led_modes_are_distinct)
{
  MTS_ASSERT_NE(LED_MODE_OFF, LED_MODE_MESSAGE);
  MTS_ASSERT_NE(LED_MODE_OFF, LED_MODE_AMPLITUDE);
  MTS_ASSERT_NE(LED_MODE_MESSAGE, LED_MODE_AMPLITUDE);
}

MTS_TEST(zero_initialized_is_off)
{
  Led_Command cmd = { 0 };
  MTS_ASSERT_EQ(cmd.mode, LED_MODE_OFF);
}

MTS_TEST(longest_text_fits)
{
  Led_Command cmd = { 0 };
  const char* msg = "HOLD STILL 1234"; // 15 characters, the maximum
  MTS_ASSERT_EQ(strlen(msg), LED_TEXT_BYTES - 1);

  strcpy(cmd.text, msg);
  MTS_ASSERT_EQ(cmd.text[LED_TEXT_BYTES - 1], '\0');
}

MTS_TEST(survives_mailbox)
{
  Mailbox* mb = mb_create(sizeof(Led_Command));
  MTS_ASSERT(mb != NULL);

  Led_Command in;
  memset(&in, 0, sizeof(in));
  in.mode = LED_MODE_MESSAGE;
  in.level = 0.5f;
  strcpy(in.text, "HOLD STILL");
  in.msg_id = 3;

  Led_Command out;
  memset(&out, 0xFF, sizeof(out));
  mb_write(mb, &in);
  bool got = mb_read(mb, &out);
  mb_destroy(mb);

  MTS_ASSERT(got);
  MTS_ASSERT_MEM_EQ(&in, &out, sizeof(Led_Command));
}

static const Mts_Test tests[] = {
  MTS_TEST_ENTRY(led_modes_are_distinct),
  MTS_TEST_ENTRY(zero_initialized_is_off),
  MTS_TEST_ENTRY(longest_text_fits),
  MTS_TEST_ENTRY(survives_mailbox),
};

MTS_TEST_MAIN(tests)

