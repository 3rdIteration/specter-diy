enum { UI_ENROLL = 1, UI_SAME, UI_OTHER, UI_REMOVE };
static unsigned fp_next_action, fp_requested, fp_pressed_action;
static lv_obj_t *fp_label, *fp_primary, *fp_primary_label, *fp_remove;

static void fp_show(const char *text) {
    lv_label_set_text(fp_label, text);
    lv_obj_align(fp_label, NULL, LV_ALIGN_CENTER, 0, -100);
    lv_refr_now(NULL);
}

static void fp_primary_event(lv_obj_t *object, lv_event_t event) {
    (void)object;
    if (event == LV_EVENT_PRESSED) fp_pressed_action = fp_next_action;
    if (fp_pressed_action == fp_next_action &&
        ((fp_next_action == UI_ENROLL && event == LV_EVENT_LONG_PRESSED) ||
         (fp_next_action != UI_ENROLL && event == LV_EVENT_CLICKED)))
        fp_requested = fp_next_action;
}

static void fp_remove_event(lv_obj_t *object, lv_event_t event) {
    (void)object;
    if (event == LV_EVENT_LONG_PRESSED) fp_requested = UI_REMOVE;
}

static void fp_buttons(unsigned action, const char *title, bool remove) {
    fp_next_action = action;
    fp_pressed_action = 0;
    lv_obj_set_hidden(fp_primary, action == 0);
    if (action) lv_label_set_text(fp_primary_label, title);
    lv_obj_set_hidden(fp_remove, !remove);
}

static void fp_progress(unsigned steps, uint8_t status) {
    char text[200];
    if (status)
        snprintf(text, sizeof(text), "Enrollment %u / 8\n\nSensor status 0x%02x\n"
                 "Lift and reposition the same finger.", steps, status);
    else
        snprintf(text, sizeof(text), "Enrollment %u / 8\n\nLift and touch again.\n"
                 "Use a different part of the same finger.", steps);
    fp_show(text);
}

static int fp_check_count(void) {
    int count = board_fingerprint_test(FP_TEST_COUNT, NULL);
    board_fingerprint_test_report(FP_REPORT_COUNT, count);
    return count;
}

static void fp_cleanup(const char *match_summary) {
    fp_show("Checking template count before cleanup...");
    int count = fp_check_count();
    int removal = -1;
    if (count == 1) {
        removal = board_fingerprint_test(FP_TEST_REMOVE, NULL);
        board_fingerprint_test_report(FP_REPORT_DELETE, removal);
        count = fp_check_count();
    }
    char text[240];
    if (count == 0) {
        snprintf(text, sizeof(text), "%s\n\nCleanup verified.\n0 enrolled templates.\n\n"
                 "Hold power to return to recovery.", match_summary);
        fp_buttons(0, "", false);
    } else {
        snprintf(text, sizeof(text), "%s\n\nCleanup unverified.\nCount: %d   Delete: %d\n"
                 "Hold the cleanup button to retry.", match_summary, count, removal);
        fp_buttons(0, "", count <= 1);
    }
    fp_show(text);
}

static void fingerprint_test_ui(void) {
    fp_label = lv_label_create(lv_scr_act(), NULL);
    lv_label_set_align(fp_label, LV_LABEL_ALIGN_CENTER);
    fp_primary = lv_btn_create(lv_scr_act(), NULL);
    lv_obj_set_size(fp_primary, 400, 72);
    lv_obj_align(fp_primary, NULL, LV_ALIGN_CENTER, 0, 115);
    lv_obj_set_event_cb(fp_primary, fp_primary_event);
    fp_primary_label = lv_label_create(fp_primary, NULL);
    fp_remove = lv_btn_create(lv_scr_act(), NULL);
    lv_obj_set_size(fp_remove, 400, 72);
    lv_obj_align(fp_remove, NULL, LV_ALIGN_CENTER, 0, 215);
    lv_obj_set_event_cb(fp_remove, fp_remove_event);
    lv_label_set_text(lv_label_create(fp_remove, NULL), "Hold to delete test slot 0");
    fp_buttons(0, "", false);
    fp_show("Fingerprint scanner test\n\nChecking the existing pairing...");
    int count = fp_check_count();
    if (count == 0) {
        fp_show("Fingerprint scanner test\n\n0 enrolled templates.\n"
                "Hold the button to enroll one test finger.");
        fp_buttons(UI_ENROLL, "Hold to enroll", false);
    } else if (count == 1) {
        fp_show("One template already exists.\n\n"
                "Only use cleanup if this test\npreviously enrolled that finger.");
        fp_buttons(0, "", true);
    } else {
        char text[145];
        snprintf(text, sizeof(text), "Cannot start enrollment.\n\nTemplate count result: %d\n"
                 "Restart to check again.", count);
        fp_show(text);
    }

    for (;;) {
        board_display_update(10);
        board_delay(10);
        unsigned action = fp_requested;
        fp_requested = 0;
        if (!action) continue;
        fp_buttons(0, "", false);
        if (action == UI_ENROLL) {
            // Check again immediately before writing the selected slot.
            if (fp_check_count() != 0) {
                fp_show("Enrollment blocked.\nRestart to check template count.");
                continue;
            }
            fp_show("Enrollment 0 / 8\n\nTouch the fingerprint sensor.\nLift and repeat as prompted.");
            int result = board_fingerprint_test(FP_TEST_ENROLL, fp_progress);
            board_fingerprint_test_report(FP_REPORT_ENROLLMENT, result);
            if (!result && fp_check_count() == 1) {
                fp_show("Enrollment verified.\n\nLift your finger, tap Start scan,\n"
                        "then touch with the enrolled finger.");
                fp_buttons(UI_SAME, "Start enrolled-finger scan", true);
            } else {
                char text[135];
                snprintf(text, sizeof(text), "Enrollment unverified (%d).", result);
                fp_cleanup(text);
            }
        } else if (action == UI_SAME || action == UI_OTHER) {
            fp_show(action == UI_SAME ? "Touch with the enrolled finger now."
                                      : "Touch with a different finger now.");
            int result = board_fingerprint_test(FP_TEST_MATCH, NULL);
            board_fingerprint_test_report(action == UI_SAME ? FP_REPORT_ENROLLED_SCAN :
                                          FP_REPORT_OTHER_SCAN, result);
            if (action == UI_SAME && result == 0) {
                fp_show("Enrolled finger matched.\n\nLift your finger, tap Start scan,\n"
                        "then touch with a different finger.");
                fp_buttons(UI_OTHER, "Start different-finger scan", true);
            } else if (action == UI_OTHER && (result == 0x81 || result == 0)) {
                fp_cleanup(result == 0x81 ? "Enrolled finger matched.\nDifferent finger rejected."
                                          : "Test failed.\nDifferent finger matched.");
            } else {
                char text[200];
                if (result < 0)
                    snprintf(text, sizeof(text), "Scan inconclusive (transport %d).\n\n"
                             "Retry or delete the test template.", result);
                else
                    snprintf(text, sizeof(text), "Scan inconclusive (module 0x%02x).\n\n"
                             "Retry or delete the test template.", result);
                fp_show(text);
                fp_buttons(action, "Retry scan", true);
            }
        } else if (action == UI_REMOVE) {
            fp_cleanup("Fingerprint test stopped.");
        }
    }
}
