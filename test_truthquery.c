/*
#   Ghost[test_truthquery:test_truthquery Domain:Validation Purpose:TruthQuery_Test_Harness
#   Owns:TestCases|Verification|ResultReporting
#   Accepts:none Returns:test_results|pass_fail_counts
#   Requires:TruthQuery|SQLite3 Exposes:main|test_all
#   State:test_results|pass|fail Health:test_coverage Tags:test,validation,truthquery ]
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "TruthQuery.c"

int test_count = 0;
int pass_count = 0;
int fail_count = 0;

void test_report(const char * test_name, int passed) {
    test_count++;
    if (passed) {
        pass_count++;
        printf("[PASS] %s\n", test_name);
    } else {
        fail_count++;
        printf("[FAIL] %s\n", test_name);
    }
}

int main(void) {
    TruthQuery_State * tq;
    TruthQuery_Result r;
    int rc;
    
    printf("=== TruthQuery Test Harness ===\n\n");
    
    /* Test 1: Create */
    tq = TruthQuery_create();
    test_report("TruthQuery_create", tq != NULL);
    
    /* Test 2: Boot with internal DB */
    rc = TruthQuery_boot(tq, NULL);
    test_report("TruthQuery_boot (internal)", rc == 0);
    
    /* Test 3: Add lesson */
    rc = TruthQuery_lesson_put(tq, "database_initialization", "Database initialization starts in BootResolver", "architecture");
    test_report("TruthQuery_lesson_put", rc == 1);
    
    /* Test 4: Add another lesson */
    rc = TruthQuery_lesson_put(tq, "vbstyle_rules", "No file imports another file", "architecture");
    test_report("TruthQuery_lesson_put (second)", rc == 1);
    
    /* Test 5: Find lessons */
    r = TruthQuery_find_lessons(tq, "database");
    test_report("TruthQuery_find_lessons", r.status == 0 && r.count > 0);
    if (r.value) {
        printf("  Result: %s\n", r.value);
        free(r.value);
    }
    if (r.error) free(r.error);
    
    /* Test 6: Record failure */
    int lesson_id = 1;
    rc = TruthQuery_failure_record(tq, "database_init", "Failed to open database file", lesson_id);
    test_report("TruthQuery_failure_record", rc == 1);
    
    /* Test 7: Query failures */
    r = TruthQuery_what_failed(tq, "database");
    test_report("TruthQuery_what_failed", r.status == 0 && r.count > 0);
    if (r.value) {
        printf("  Result: %s\n", r.value);
        free(r.value);
    }
    if (r.error) free(r.error);
    
    /* Test 8: Add authority nodes */
    rc = TruthQuery_authority_add(tq, "DatabaseManager", "class", "storage", NULL);
    test_report("TruthQuery_authority_add (root)", rc == 1);
    
    rc = TruthQuery_authority_add(tq, "SqliteHelper", "class", "mutation", "DatabaseManager");
    test_report("TruthQuery_authority_add (child)", rc == 1);
    
    rc = TruthQuery_authority_add(tq, "BootResolver", "function", "orchestration", "DatabaseManager");
    test_report("TruthQuery_authority_add (second child)", rc == 1);
    
    /* Test 9: Show dependencies */
    r = TruthQuery_show_dependencies(tq, "DatabaseManager");
    test_report("TruthQuery_show_dependencies", r.status == 0 && r.count >= 3);
    if (r.value) {
        printf("  Result: %s\n", r.value);
        free(r.value);
    }
    if (r.error) free(r.error);
    
    /* Test 10: Authority graph */
    r = TruthQuery_authority_graph(tq, NULL);
    test_report("TruthQuery_authority_graph", r.status == 0 && r.count >= 3);
    if (r.value) {
        printf("  Result: %s\n", r.value);
        free(r.value);
    }
    if (r.error) free(r.error);
    
    /* Test 11: Where is (with verb registry data) */
    r = TruthQuery_where_is(tq, "boot");
    test_report("TruthQuery_where_is", r.status == 0);
    if (r.value) {
        printf("  Result: %s\n", r.value);
        free(r.value);
    }
    if (r.error) free(r.error);
    
    /* Test 12: Trace execution (with execution_route data) */
    r = TruthQuery_trace_execution(tq, "MemUnit", NULL);
    test_report("TruthQuery_trace_execution", r.status == 0);
    if (r.value) {
        printf("  Result: %s\n", r.value);
        free(r.value);
    }
    if (r.error) free(r.error);
    
    /* Test 13: Cleanup */
    rc = TruthQuery_cleanup(tq);
    test_report("TruthQuery_cleanup", rc == 1);
    
    free(tq);
    
    printf("\n=== Test Summary ===\n");
    printf("Total: %d\n", test_count);
    printf("Passed: %d\n", pass_count);
    printf("Failed: %d\n", fail_count);
    printf("Success Rate: %.1f%%\n", test_count > 0 ? (pass_count * 100.0 / test_count) : 0.0);
    
    return fail_count > 0 ? 1 : 0;
}
