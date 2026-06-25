/**
 * Strong implementation supplied via amend; expected to win over the weak
 * symbol in boards/test_amend_features/setup_device.c thanks to the generated
 * component's WHOLE_ARCHIVE property.
 */
int bmgr_test_amend_status_led_default_level(void)
{
    return 1;
}
