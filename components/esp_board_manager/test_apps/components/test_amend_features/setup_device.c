/**
 * Base implementation used by the amend-feature fixture.
 *
 * Amend fragments may provide strong versions of these weak functions, so the
 * generated component can exercise board-local C source override behavior via
 * the WHOLE_ARCHIVE property.
 */
int __attribute__((weak)) bmgr_test_amend_status_led_default_level(void)
{
    return 0;
}

int __attribute__((weak)) bmgr_test_amend_extra_power_channel_count(void)
{
    return 0;
}
