/* This C source is intentionally not listed in board_amend.yaml's apply: list.
 * It must NOT be compiled by the generated component (would yield a duplicate
 * symbol with pack_setup.c if it were).
 */
int bmgr_test_amend_unused_marker(void)
{
    return 0xDEAD;
}
