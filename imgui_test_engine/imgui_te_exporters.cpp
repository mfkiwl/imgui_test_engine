// dear imgui
// (test engine, exporters)

#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "imgui_te_exporters.h"
#include "imgui_te_engine.h"
#include "imgui_te_internal.h"
#include "thirdparty/Str/Str.h"

//-------------------------------------------------------------------------
// [SECTION] FORWARD DECLARATIONS
//-------------------------------------------------------------------------

static void ImGuiTestEngine_ExportJUnitXml(ImGuiTestEngine* engine, const char* output_file);

//-------------------------------------------------------------------------
// [SECTION] TEST ENGINE EXPORTER FUNCTIONS
//-------------------------------------------------------------------------
// - ImGuiTestEngine_Export()
// - ImGuiTestEngine_ExportEx()
// - ImGuiTestEngine_ExportJUnitXml()
//-------------------------------------------------------------------------

// This is mostly a copy of ImGuiTestEngine_PrintResultSummary with few additions.
static void ImGuiTestEngine_ExportResultSummary(ImGuiTestEngine* engine, FILE* fp, int indent_count, ImGuiTestGroup group)
{
    int count_tested = 0;
    int count_success = 0;

    for (ImGuiTest* test : engine->TestsAll)
    {
        if (test->Group != group)
            continue;
        if (test->Status != ImGuiTestStatus_Unknown)
            count_tested++;
        if (test->Status == ImGuiTestStatus_Success)
            count_success++;
    }

    Str64 indent_str;
    indent_str.reserve(indent_count + 1);
    memset(indent_str.c_str(), ' ', indent_count);
    indent_str[indent_count] = 0;
    const char* indent = indent_str.c_str();

    if (count_success < count_tested)
    {
        fprintf(fp, "\n%sFailing tests:\n", indent);
        for (ImGuiTest* test : engine->TestsAll)
        {
            if (test->Group != group)
                continue;
            if (test->Status == ImGuiTestStatus_Error)
                fprintf(fp, "%s- %s\n", indent, test->Name);
        }
        fprintf(fp, "\n");
    }

    fprintf(fp, "%sTests Result: %s\n", indent, (count_success == count_tested) ? "OK" : "KO");
    fprintf(fp, "%s(%d/%d tests passed)\n", indent, count_success, count_tested);
}

static bool ImGuiTestEngine_HasAnyLogLines(ImGuiTestLog* test_log, ImGuiTestVerboseLevel level)
{
    for (auto& line_info : test_log->LineInfoAll)
        if (line_info.Level <= level)
            return true;
    return false;
}

static void ImGuiTestEngine_PrintLogLines(FILE* fp, ImGuiTestLog* test_log, int indent, ImGuiTestVerboseLevel level)
{
    Str128 log_line;
    for (auto& line_info : test_log->LineInfoAll)
    {
        if (line_info.Level > level)
            continue;
        const char* line_start = test_log->Buffer.c_str() + line_info.LineOffset;
        const char* line_end = strstr(line_start, "\n");
        log_line.set(line_start, line_end);
        ImStrXmlEscape(&log_line);
        for (int i = 0; i < indent; i++)
            fprintf(fp, " ");
        fprintf(fp, "%s\n", log_line.c_str());
    }
}

void ImGuiTestEngine_Export(ImGuiTestEngine* engine)
{
    ImGuiTestEngineIO& io = engine->IO;
    ImGuiTestEngine_ExportEx(engine, io.ExportResultsFormat, io.ExportResultsFilename);
}

void ImGuiTestEngine_ExportEx(ImGuiTestEngine* engine, ImGuiTestEngineExportFormat format, const char* filename)
{
    if (format == ImGuiTestEngineExportFormat_None)
        return;
    IM_ASSERT(filename != NULL);

    if (format == ImGuiTestEngineExportFormat_JUnitXml)
        ImGuiTestEngine_ExportJUnitXml(engine, filename);
    else
        IM_ASSERT(0);
}

void ImGuiTestEngine_ExportJUnitXml(ImGuiTestEngine* engine, const char* output_file)
{
    IM_ASSERT(engine != NULL);
    IM_ASSERT(output_file != NULL);

    FILE* fp = fopen(output_file, "w+b");
    if (fp == NULL)
    {
        fprintf(stderr, "Writing '%s' failed.\n", output_file);
        return;
    }

    // Per-testsuite test statistics.
    struct
    {
        const char* Name     = NULL;
        int         Tests    = 0;
        int         Failures = 0;
        int         Disabled = 0;
    } testsuites[ImGuiTestGroup_COUNT];
    testsuites[ImGuiTestGroup_Tests].Name = "tests";
    testsuites[ImGuiTestGroup_Perfs].Name = "perfs";

    for (int n = 0; n < engine->TestsAll.Size; n++)
    {
        ImGuiTest* test = engine->TestsAll[n];
        auto* stats = &testsuites[test->Group];
        stats->Tests += 1;
        if (test->Status == ImGuiTestStatus_Error)
            stats->Failures += 1;
        else if (test->Status == ImGuiTestStatus_Unknown)
            stats->Disabled += 1;
    }

    // Attributes for <testsuites> tag.
    const char* testsuites_name = "Dear ImGui";
    int testsuites_failures = 0;
    int testsuites_tests = 0;
    int testsuites_disabled = 0;
    float testsuites_time = (float)((double)(engine->EndTime - engine->StartTime) / 1000000.0);
    for (int testsuite_id = ImGuiTestGroup_Tests; testsuite_id < ImGuiTestGroup_COUNT; testsuite_id++)
    {
        testsuites_tests += testsuites[testsuite_id].Tests;
        testsuites_failures += testsuites[testsuite_id].Failures;
        testsuites_disabled += testsuites[testsuite_id].Disabled;
    }

    // FIXME: "errors" attribute and <error> tag in <testcase> may be supported if we have means to catch unexpected errors like assertions.
    fprintf(fp, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<testsuites disabled=\"%d\" errors=\"0\" failures=\"%d\" name=\"%s\" tests=\"%d\" time=\"%.3f\">\n",
        testsuites_disabled, testsuites_failures, testsuites_name, testsuites_tests, testsuites_time);

    const char* teststatus_names[] = { "skipped", "success", "queued", "running", "error", "suspended" };
    for (int testsuite_id = ImGuiTestGroup_Tests; testsuite_id < ImGuiTestGroup_COUNT; testsuite_id++)
    {
        // Attributes for <testsuite> tag.
        auto* testsuite = &testsuites[testsuite_id];
        float testsuite_time = testsuites_time;         // FIXME: We do not differentiate between tests and perfs, they are executed in one big batch.
        Str30 testsuite_timestamp = "";
        ImTimestampToISO8601(engine->StartTime, &testsuite_timestamp);
        fprintf(fp, "  <testsuite name=\"%s\" tests=\"%d\" disabled=\"%d\" errors=\"0\" failures=\"%d\" hostname=\"\" id=\"%d\" package=\"\" skipped=\"0\" time=\"%.3f\" timestamp=\"%s\">\n",
            testsuite->Name, testsuite->Tests, testsuite->Disabled, testsuite->Failures, testsuite_id, testsuite_time, testsuite_timestamp.c_str());

        for (int n = 0; n < engine->TestsAll.Size; n++)
        {
            ImGuiTest* test = engine->TestsAll[n];
            if (test->Group != testsuite_id)
                continue;

            // Attributes for <testcase> tag.
            const char* testcase_name = test->Name;
            const char* testcase_classname = test->Category;
            const char* testcase_status = teststatus_names[test->Status + 1];   // +1 because _Unknown status is -1.
            float testcase_time = (float)((double)(test->EndTime - test->StartTime) / 1000000.0);

            fprintf(fp, "    <testcase name=\"%s\" assertions=\"0\" classname=\"%s\" status=\"%s\" time=\"%.3f\">\n",
                testcase_name, testcase_classname, testcase_status, testcase_time);

            if (test->Status == ImGuiTestStatus_Error)
            {
                // Skip last error message because it is generic information that test failed.
                Str128 log_line;
                for (int i = test->TestLog.LineInfo.Size - 2; i >= 0; i--)
                {
                    ImGuiTestLogLineInfo* line_info = &test->TestLog.LineInfo[i];
                    if (line_info->Level > engine->IO.ConfigVerboseLevelOnError)
                        continue;
                    if (line_info->Level == ImGuiTestVerboseLevel_Error)
                    {
                        const char* line_start = test->TestLog.Buffer.c_str() + line_info->LineOffset;
                        const char* line_end = strstr(line_start, "\n");
                        log_line.set(line_start, line_end);
                        ImStrXmlEscape(&log_line);
                        break;
                    }
                }

                // Failing tests save their "on error" log output in text element of <failure> tag.
                fprintf(fp, "      <failure message=\"%s\" type=\"error\">\n", log_line.c_str());
                ImGuiTestEngine_PrintLogLines(fp, &test->TestLog, 8, engine->IO.ConfigVerboseLevelOnError);
                fprintf(fp, "      </failure>\n");
            }

            if (test->Status == ImGuiTestStatus_Unknown)
            {
                fprintf(fp, "      <skipped message=\"Skipped\" />\n");
            }
            else
            {
                // Succeeding tests save their defaiult log output output as "stdout".
                if (ImGuiTestEngine_HasAnyLogLines(&test->TestLog, engine->IO.ConfigVerboseLevel))
                {
                    fprintf(fp, "      <system-out>\n");
                    ImGuiTestEngine_PrintLogLines(fp, &test->TestLog, 8, engine->IO.ConfigVerboseLevel);
                    fprintf(fp, "      </system-out>\n");
                }

                // Save error messages as "stderr".
                if (ImGuiTestEngine_HasAnyLogLines(&test->TestLog, ImGuiTestVerboseLevel_Error))
                {
                    fprintf(fp, "      <system-err>\n");
                    ImGuiTestEngine_PrintLogLines(fp, &test->TestLog, 8, ImGuiTestVerboseLevel_Error);
                    fprintf(fp, "      </system-err>\n");
                }
            }
            fprintf(fp, "    </testcase>\n");
        }

        if (testsuites[testsuite_id].Disabled < testsuites[testsuite_id].Tests) // Any tests executed
        {
            // Log all log messages as "stdout".
            fprintf(fp, "    <system-out>\n");
            for (int n = 0; n < engine->TestsAll.Size; n++)
            {
                ImGuiTest* test = engine->TestsAll[n];
                if (test->Group != testsuite_id)
                    continue;
                if (test->Status == ImGuiTestStatus_Unknown)
                    continue;
                fprintf(fp, "      [0000] Test: '%s' '%s'..\n", test->Category, test->Name);
                ImGuiTestVerboseLevel level = test->Status == ImGuiTestStatus_Error ? engine->IO.ConfigVerboseLevelOnError : engine->IO.ConfigVerboseLevel;
                ImGuiTestEngine_PrintLogLines(fp, &test->TestLog, 6, level);
            }
            ImGuiTestEngine_ExportResultSummary(engine, fp, 6, (ImGuiTestGroup)testsuite_id);
            fprintf(fp, "    </system-out>\n");

            // Log all warning and error messages as "stderr".
            fprintf(fp, "    <system-err>\n");
            for (int n = 0; n < engine->TestsAll.Size; n++)
            {
                ImGuiTest* test = engine->TestsAll[n];
                if (test->Group != testsuite_id)
                    continue;
                if (test->Status == ImGuiTestStatus_Unknown)
                    continue;
                fprintf(fp, "      [0000] Test: '%s' '%s'..\n", test->Category, test->Name);
                ImGuiTestEngine_PrintLogLines(fp, &test->TestLog, 6, ImGuiTestVerboseLevel_Warning);
            }
            ImGuiTestEngine_ExportResultSummary(engine, fp, 6, (ImGuiTestGroup)testsuite_id);
            fprintf(fp, "    </system-err>\n");
        }
        fprintf(fp, "  </testsuite>\n");
    }
    fprintf(fp, "</testsuites>\n");
    fclose(fp);
    fprintf(stdout, "Saved test results to '%s' successfully.\n", output_file);
}