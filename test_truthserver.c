/*
#   Ghost[test_truthserver:test_truthserver Domain:Validation Purpose:TruthServer_Test_Harness
#   Owns:TestCases|IntegrationVerification|ResultReporting
#   Accepts:none Returns:test_results|pass_fail_counts
#   Requires:TruthServer|MemUnit|TruthQuery|SQLite3 Exposes:main|test_all
#   State:test_results|pass|fail Health:integration_coverage Tags:test,validation,truthserver,integration ]
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct TruthServer_State TruthServer_State;
typedef struct TruthServer_Result {
    char * value;
    char * error;
    int status;
    int count;
} TruthServer_Result;

TruthServer_State * TruthServer_create(void);
int TruthServer_boot(TruthServer_State * self, const char * config);
int TruthServer_seed_lessons(TruthServer_State * self);
int TruthServer_seed_authority_graph(TruthServer_State * self);
TruthServer_Result TruthServer_query(TruthServer_State * self, const char * query_type, const char * query_value);
TruthServer_Result TruthServer_find_path(TruthServer_State * self, const char * source_class, const char * target_class);
TruthServer_Result TruthServer_record_decision(TruthServer_State * self, const char * problem, const char * discussion, const char * decision, const char * result, const char * outcome, const char * context);
TruthServer_Result TruthServer_health(TruthServer_State * self);
int TruthServer_cleanup(TruthServer_State * self);

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
    TruthServer_State * ts;
    TruthServer_Result r;
    int rc;
    
    printf("=== TruthServer Test Harness ===\n\n");
    
    /* Test 1: Create */
    ts = TruthServer_create();
    test_report("TruthServer_create", ts != NULL);
    
    /* Test 2: Boot with in-memory DB */
    rc = TruthServer_boot(ts, NULL);
    if (rc != 0) {
        printf("  Boot failed with code: %d\n", rc);
    }
    test_report("TruthServer_boot (in-memory)", rc == 0);
    
    /* Test 3: Seed lessons */
    rc = TruthServer_seed_lessons(ts);
    test_report("TruthServer_seed_lessons", rc >= 10);
    printf("  Lessons seeded: %d\n", rc);
    
    /* Test 4: Seed authority graph */
    rc = TruthServer_seed_authority_graph(ts);
    test_report("TruthServer_seed_authority_graph", rc >= 6);
    printf("  Authority nodes seeded: %d\n", rc);
    
    /* Test 5: Query - where_is */
    r = TruthServer_query(ts, "where_is", "database");
    test_report("TruthServer_query (where_is)", r.status == 0 && r.count > 0);
    if (r.value) {
        printf("  Result: %s\n", r.value);
        free(r.value);
    }
    if (r.error) free(r.error);
    
    /* Test 6: Query - find_lessons */
    r = TruthServer_query(ts, "find_lessons", "maxed");
    test_report("TruthServer_query (find_lessons)", r.status == 0 && r.count > 0);
    if (r.value) {
        printf("  Result: %s\n", r.value);
        free(r.value);
    }
    if (r.error) free(r.error);
    
    /* Test 7: Query - show_dependencies */
    r = TruthServer_query(ts, "show_dependencies", "DatabaseManager");
    test_report("TruthServer_query (show_dependencies)", r.status == 0 && r.count > 0);
    if (r.value) {
        printf("  Result: %s\n", r.value);
        free(r.value);
    }
    if (r.error) free(r.error);
    
    /* Test 8: Query - authority_graph */
    r = TruthServer_query(ts, "authority_graph", "MemUnit");
    test_report("TruthServer_query (authority_graph)", r.status == 0 && r.count > 0);
    if (r.value) {
        printf("  Result: %s\n", r.value);
        free(r.value);
    }
    if (r.error) free(r.error);
    
    /* Test 9: Record decision */
    r = TruthServer_record_decision(ts, "Which database to use?", "Discussed SQLite vs PostgreSQL", "Use SQLite for simplicity", "Simple implementation", "Working system", "Core_Memory domain");
    test_report("TruthServer_record_decision", r.status == 0 && r.count > 0);
    if (r.value) {
        printf("  Decision ID: %d\n", r.count);
        free(r.value);
    }
    if (r.error) free(r.error);
    
    /* Test 10: Health check */
    r = TruthServer_health(ts);
    test_report("TruthServer_health", r.status == 0);
    if (r.value) {
        printf("  Result: %s\n", r.value);
        free(r.value);
    }
    if (r.error) free(r.error);
    
    /* Test 11: Cleanup */
    rc = TruthServer_cleanup(ts);
    test_report("TruthServer_cleanup", rc == 1);
    
    free(ts);
    
    printf("\n=== Test Summary ===\n");
    printf("Total: %d\n", test_count);
    printf("Passed: %d\n", pass_count);
    printf("Failed: %d\n", fail_count);
    printf("Success Rate: %.1f%%\n", test_count > 0 ? (pass_count * 100.0 / test_count) : 0.0);
    
    return fail_count > 0 ? 1 : 0;
}
