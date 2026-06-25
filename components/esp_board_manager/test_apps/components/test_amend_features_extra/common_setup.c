/**
 * Out-of-tree amend fragment used to verify that ../ relative paths in
 * board_amend.yaml's apply: list are resolved against the manifest directory
 * and compiled into the generated component.
 */
int bmgr_test_amend_common_extra_marker(void)
{
    return 0xABCD;
}
