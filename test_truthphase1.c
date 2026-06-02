/*
#   Ghost[test_truthphase1:test_truthphase1 Domain:Validation Purpose:Phase1_Test_Harness
#   Owns:TestCases|MethodRetrieval|ChatRetrieval|DocumentRetrieval|DistilledTruth
#   Accepts:none Returns:test_results|pass_fail_counts
#   Requires:TruthPhase1|SQLite3 Exposes:main|test_all
#   State:test_results|pass|fail Health:phase1_coverage Tags:test,validation,phase1,retrieval ]
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>

typedef struct TruthPhase1_State TruthPhase1_State;
typedef struct TruthPhase1_Result {
    char * value;
    char * error;
    int status;
    int count;
} TruthPhase1_Result;

TruthPhase1_State * TruthPhase1_create(void);
int TruthPhase1_boot(TruthPhase1_State * self, sqlite3 * db);
TruthPhase1_Result TruthPhase1_insert_method(TruthPhase1_State * self, const char * language, const char * method_name, const char * description, const char * code, const char * source_file, int line_start, int line_end);
TruthPhase1_Result TruthPhase1_insert_chat(TruthPhase1_State * self, const char * chat_title, const char * chat_content, const char * source_file);
TruthPhase1_Result TruthPhase1_insert_document(TruthPhase1_State * self, const char * doc_title, const char * doc_content, const char * source_file);
TruthPhase1_Result TruthPhase1_search_methods(TruthPhase1_State * self, const char * language, const char * query);
TruthPhase1_Result TruthPhase1_search_chats(TruthPhase1_State * self, const char * query);
TruthPhase1_Result TruthPhase1_search_documents(TruthPhase1_State * self, const char * query);
TruthPhase1_Result TruthPhase1_distill_truth(TruthPhase1_State * self, const char * query);
int TruthPhase1_cleanup(TruthPhase1_State * self);

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
    TruthPhase1_State * tp1;
    TruthPhase1_Result r;
    sqlite3 * db;
    int rc;
    
    printf("=== TruthPhase1 Test Harness ===\n\n");
    
    /* Test 1: Create */
    tp1 = TruthPhase1_create();
    test_report("TruthPhase1_create", tp1 != NULL);
    
    /* Test 2: Boot with in-memory DB */
    rc = sqlite3_open(":memory:", &db);
    test_report("sqlite3_open", rc == SQLITE_OK);
    
    rc = TruthPhase1_boot(tp1, db);
    test_report("TruthPhase1_boot", rc == 0);
    
    /* Test 3: Insert Python method */
    r = TruthPhase1_insert_method(tp1, "python", "load_configuration", "Loads startup configuration and validates paths", "def load_configuration():\n    config = read_config()\n    validate_paths(config)\n    return config", "/Users/waynephilliplundall/testbed/core/PY/Core_Memory/Core_Memory.py", 45, 50);
    test_report("TruthPhase1_insert_method (python)", r.status == 0 && r.count > 0);
    if (r.value) free(r.value);
    if (r.error) free(r.error);
    
    /* Test 4: Insert Swift method */
    r = TruthPhase1_insert_method(tp1, "swift", "initializeDatabase", "Initializes the database connection", "func initializeDatabase() -> DatabaseConnection {\n    let connection = Database.connect()\n    return connection\n}", "/Users/waynephilliplundall/testbed/core/SWIFT/Core_Memory/Core_Memory.swift", 30, 35);
    test_report("TruthPhase1_insert_method (swift)", r.status == 0 && r.count > 0);
    if (r.value) free(r.value);
    if (r.error) free(r.error);
    
    /* Test 5: Insert C method */
    r = TruthPhase1_insert_method(tp1, "c", "boot_resolver", "Handles system boot sequence", "int boot_resolver(const char * config) {\n    return validate_config(config);\n}", "/Users/waynephilliplundall/testbed/core/C/Core_Memory/Core_Memory.c", 120, 125);
    test_report("TruthPhase1_insert_method (c)", r.status == 0 && r.count > 0);
    if (r.value) free(r.value);
    if (r.error) free(r.error);
    
    /* Test 6: Insert chat */
    r = TruthPhase1_insert_chat(tp1, "Database initialization discussion", "User: Where does database initialization happen?\nAssistant: It happens in BootResolver.start() which calls ConfigSetup.validate() which calls DatabaseManager.initialize()", "/Users/waynephilliplundall/testbed/Db/Memories/chat_001.md");
    test_report("TruthPhase1_insert_chat", r.status == 0 && r.count > 0);
    if (r.value) free(r.value);
    if (r.error) free(r.error);
    
    /* Test 7: Insert document */
    r = TruthPhase1_insert_document(tp1, "Architecture Overview", "The system uses a layered architecture with MemUnit as the foundation. TruthQuery provides the query surface. TruthSchema provides the structured truth tables.", "/Users/waynephilliplundall/testbed/Docs/architecture.md");
    test_report("TruthPhase1_insert_document", r.status == 0 && r.count > 0);
    if (r.value) free(r.value);
    if (r.error) free(r.error);
    
    /* Test 8: Search Python methods */
    r = TruthPhase1_search_methods(tp1, "python", "configuration");
    test_report("TruthPhase1_search_methods (python)", r.status == 0 && r.count > 0);
    if (r.value) {
        printf("  Result: %s\n", r.value);
        free(r.value);
    }
    if (r.error) free(r.error);
    
    /* Test 9: Search chats */
    r = TruthPhase1_search_chats(tp1, "database");
    test_report("TruthPhase1_search_chats", r.status == 0 && r.count > 0);
    if (r.value) {
        printf("  Result: %s\n", r.value);
        free(r.value);
    }
    if (r.error) free(r.error);
    
    /* Test 10: Search documents */
    r = TruthPhase1_search_documents(tp1, "architecture");
    test_report("TruthPhase1_search_documents", r.status == 0 && r.count > 0);
    if (r.value) {
        printf("  Result: %s\n", r.value);
        free(r.value);
    }
    if (r.error) free(r.error);
    
    /* Test 11: Distill truth */
    r = TruthPhase1_distill_truth(tp1, "database initialization");
    test_report("TruthPhase1_distill_truth", r.status == 0);
    if (r.value) {
        printf("  Result: %s\n", r.value);
        free(r.value);
    }
    if (r.error) free(r.error);
    
    /* Test 12: Cleanup */
    rc = TruthPhase1_cleanup(tp1);
    test_report("TruthPhase1_cleanup", rc == 1);
    
    sqlite3_close(db);
    free(tp1);
    
    printf("\n=== Test Summary ===\n");
    printf("Total: %d\n", test_count);
    printf("Passed: %d\n", pass_count);
    printf("Failed: %d\n", fail_count);
    printf("Success Rate: %.1f%%\n", test_count > 0 ? (pass_count * 100.0 / test_count) : 0.0);
    
    return fail_count > 0 ? 1 : 0;
}
