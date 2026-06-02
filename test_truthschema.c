/*
#   Ghost[test_truthschema:test_truthschema Domain:Validation Purpose:TruthSchema_Test_Harness
#   Owns:TestCases|SchemaVerification|LayerTesting
#   Accepts:none Returns:test_results|pass_fail_counts
#   Requires:TruthSchema|SQLite3 Exposes:main|test_all
#   State:test_results|pass|fail Health:schema_coverage Tags:test,validation,truthschema,layers ]
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>

typedef struct TruthSchema_State TruthSchema_State;
typedef struct TruthSchema_Result {
    char * value;
    char * error;
    int status;
    int count;
} TruthSchema_Result;

TruthSchema_State * TruthSchema_create(void);
int TruthSchema_boot(TruthSchema_State * self, sqlite3 * db);
TruthSchema_Result TruthSchema_insert_class(TruthSchema_State * self, const char * class_name, const char * file_path, const char * domain, const char * authority, const char * description);
TruthSchema_Result TruthSchema_insert_method(TruthSchema_State * self, int class_id, const char * method_name, const char * authority, const char * bracket_sig, const char * magnetic_sig);
TruthSchema_Result TruthSchema_insert_file(TruthSchema_State * self, const char * path, const char * hash, const char * language, int line_count);
TruthSchema_Result TruthSchema_insert_domain(TruthSchema_State * self, const char * name, const char * authority, const char * description);
TruthSchema_Result TruthSchema_add_relationship(TruthSchema_State * self, const char * source_type, int source_id, const char * target_type, int target_id, const char * rel_type, float strength);
TruthSchema_Result TruthSchema_record_decision(TruthSchema_State * self, const char * problem, const char * discussion, const char * decision, const char * result, const char * outcome, const char * context);
TruthSchema_Result TruthSchema_find_path(TruthSchema_State * self, const char * source_type, int source_id, const char * target_type, int target_id);
int TruthSchema_cleanup(TruthSchema_State * self);

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
    TruthSchema_State * ts;
    TruthSchema_Result r;
    sqlite3 * db;
    int rc;
    int boot_resolver_id, database_manager_id, config_setup_id, file_id, domain_id;
    
    printf("=== TruthSchema Test Harness ===\n\n");
    
    /* Test 1: Create */
    ts = TruthSchema_create();
    test_report("TruthSchema_create", ts != NULL);
    
    /* Test 2: Boot with in-memory DB */
    rc = sqlite3_open(":memory:", &db);
    test_report("sqlite3_open", rc == SQLITE_OK);
    
    rc = TruthSchema_boot(ts, db);
    test_report("TruthSchema_boot", rc == 0);
    
    /* Test 3: Insert domain */
    r = TruthSchema_insert_domain(ts, "Core_Memory", "memory_orchestration", "Memory orchestration domain");
    test_report("TruthSchema_insert_domain", r.status == 0 && r.count > 0);
    domain_id = r.count;
    if (r.value) free(r.value);
    if (r.error) free(r.error);
    
    /* Test 4: Insert file */
    r = TruthSchema_insert_file(ts, "/Users/waynephilliplundall/testbed/core/PY/Core_Memory/Core_Memory.py", "abc123", "Python", 450);
    test_report("TruthSchema_insert_file", r.status == 0 && r.count > 0);
    file_id = r.count;
    if (r.value) free(r.value);
    if (r.error) free(r.error);
    
    /* Test 5: Insert class - BootResolver */
    r = TruthSchema_insert_class(ts, "BootResolver", "/Users/waynephilliplundall/testbed/core/PY/Core_Memory/Core_Memory.py", "Core_Memory", "orchestration", "Handles system boot sequence");
    test_report("TruthSchema_insert_class (BootResolver)", r.status == 0 && r.count > 0);
    boot_resolver_id = r.count;
    if (r.value) free(r.value);
    if (r.error) free(r.error);
    
    /* Test 6: Insert class - ConfigSetup */
    r = TruthSchema_insert_class(ts, "ConfigSetup", "/Users/waynephilliplundall/testbed/core/PY/Core_Memory/Core_Memory.py", "Core_Memory", "validation", "Validates configuration");
    test_report("TruthSchema_insert_class (ConfigSetup)", r.status == 0 && r.count > 0);
    config_setup_id = r.count;
    if (r.value) free(r.value);
    if (r.error) free(r.error);
    
    /* Test 7: Insert class - DatabaseManager */
    r = TruthSchema_insert_class(ts, "DatabaseManager", "/Users/waynephilliplundall/testbed/core/PY/Core_Memory/Core_Memory.py", "Core_Memory", "storage", "Manages database operations");
    test_report("TruthSchema_insert_class (DatabaseManager)", r.status == 0 && r.count > 0);
    database_manager_id = r.count;
    if (r.value) free(r.value);
    if (r.error) free(r.error);
    
    /* Test 8: Insert method - BootResolver.start */
    r = TruthSchema_insert_method(ts, boot_resolver_id, "start", "orchestration", "[BootResolver:start][Input:config][Output:status]", "magk:boot");
    test_report("TruthSchema_insert_method (start)", r.status == 0 && r.count > 0);
    if (r.value) free(r.value);
    if (r.error) free(r.error);
    
    /* Test 9: Insert method - ConfigSetup.validate */
    r = TruthSchema_insert_method(ts, config_setup_id, "validate", "validation", "[ConfigSetup:validate][Input:config][Output:valid]", "magk:validate");
    test_report("TruthSchema_insert_method (validate)", r.status == 0 && r.count > 0);
    if (r.value) free(r.value);
    if (r.error) free(r.error);
    
    /* Test 10: Insert method - DatabaseManager.initialize */
    r = TruthSchema_insert_method(ts, database_manager_id, "initialize", "storage", "[DatabaseManager:initialize][Input:config][Output:db]", "magk:init");
    test_report("TruthSchema_insert_method (initialize)", r.status == 0 && r.count > 0);
    if (r.value) free(r.value);
    if (r.error) free(r.error);
    
    /* Test 11: Add relationship - BootResolver calls ConfigSetup */
    r = TruthSchema_add_relationship(ts, "class", boot_resolver_id, "class", config_setup_id, "calls", 0.9);
    test_report("TruthSchema_add_relationship (BootResolver->ConfigSetup)", r.status == 0 && r.count > 0);
    if (r.value) free(r.value);
    if (r.error) free(r.error);
    
    /* Test 12: Add relationship - ConfigSetup calls DatabaseManager */
    r = TruthSchema_add_relationship(ts, "class", config_setup_id, "class", database_manager_id, "calls", 0.9);
    test_report("TruthSchema_add_relationship (ConfigSetup->DatabaseManager)", r.status == 0 && r.count > 0);
    if (r.value) free(r.value);
    if (r.error) free(r.error);
    
    /* Test 13: Add relationship - BootResolver owns start method */
    r = TruthSchema_add_relationship(ts, "class", boot_resolver_id, "method", 1, "owns", 1.0);
    test_report("TruthSchema_add_relationship (BootResolver->start)", r.status == 0 && r.count > 0);
    if (r.value) free(r.value);
    if (r.error) free(r.error);
    
    /* Test 14: Find path - BootResolver to DatabaseManager */
    r = TruthSchema_find_path(ts, "class", boot_resolver_id, "class", database_manager_id);
    test_report("TruthSchema_find_path (BootResolver->DatabaseManager)", r.status == 0);
    if (r.value) {
        printf("  Path: %s\n", r.value);
        free(r.value);
    }
    if (r.error) free(r.error);
    
    /* Test 15: Record decision */
    r = TruthSchema_record_decision(ts, "Which database to use?", "Discussed SQLite vs PostgreSQL", "Use SQLite for simplicity", "Simple implementation", "Working system", "Core_Memory domain");
    test_report("TruthSchema_record_decision", r.status == 0 && r.count > 0);
    if (r.value) free(r.value);
    if (r.error) free(r.error);
    
    /* Test 16: Cleanup */
    rc = TruthSchema_cleanup(ts);
    test_report("TruthSchema_cleanup", rc == 1);
    
    sqlite3_close(db);
    free(ts);
    
    printf("\n=== Test Summary ===\n");
    printf("Total: %d\n", test_count);
    printf("Passed: %d\n", pass_count);
    printf("Failed: %d\n", fail_count);
    printf("Success Rate: %.1f%%\n", test_count > 0 ? (pass_count * 100.0 / test_count) : 0.0);
    
    return fail_count > 0 ? 1 : 0;
}
