#include <check.h>
#include <stdlib.h>
#include <string.h>

/* Include the XDP program interface headers */
#include "src/xdp/program.h"

#define XDP_TEST_BUFFER_SIZE 2048

START_TEST(test_xdp_prog_test_run_no_buffer_overflow)
{
    /* Invariant: Buffer reads/writes never exceed the declared destination buffer length.
     * DataSizeIn must be validated against the allocated buffer size before memcpy. */

    struct {
        size_t size;
        const char *desc;
    } payloads[] = {
        { XDP_TEST_BUFFER_SIZE * 10, "10x overflow" },   /* exploit case */
        { XDP_TEST_BUFFER_SIZE * 2,  "2x overflow" },    /* boundary overflow */
        { XDP_TEST_BUFFER_SIZE + 1,  "off-by-one" },     /* boundary value */
        { XDP_TEST_BUFFER_SIZE / 2,  "valid input" },    /* valid case */
    };
    int num_payloads = sizeof(payloads) / sizeof(payloads[0]);

    for (int i = 0; i < num_payloads; i++) {
        size_t data_size_in = payloads[i].size;
        BYTE *data_in = (BYTE *)malloc(data_size_in);
        ck_assert_ptr_nonnull(data_in);
        memset(data_in, 'A', data_size_in);

        /* Attempt to invoke the XDP program test run with oversized input.
         * The function MUST either reject the input (return error) or
         * truncate the copy to the internal buffer size. It must NOT
         * perform an unbounded memcpy. */
        HRESULT hr = XdpProgramTestRunInvoke(data_in, (UINT32)data_size_in);

        if (data_size_in > XDP_TEST_BUFFER_SIZE) {
            /* Oversized input must be rejected */
            ck_assert_msg(FAILED(hr),
                "Expected rejection for oversized input (%zu bytes): %s",
                data_size_in, payloads[i].desc);
        } else {
            /* Valid input should succeed */
            ck_assert_msg(SUCCEEDED(hr),
                "Expected success for valid input (%zu bytes): %s",
                data_size_in, payloads[i].desc);
        }

        free(data_in);
    }
}
END_TEST

Suite *security_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Security");
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_xdp_prog_test_run_no_buffer_overflow);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = security_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}