#include <stdio.h>
#include <stdlib.h>
#include "sql_parser.h"

int main() {
    printf("--- LoMo SQL Shell (MVP) ---\n");
    
    // Example 1: Basic In-place Filter
    const char* sql1 = "SELECT SUM(1) FROM 'merged_part' WHERE 1 > 50";
    printf("\nSQL: %s\n", sql1);
    LomoQueryPlan plan1 = lomo_parse_sql(sql1);
    lomo_execute_plan(&plan1);

    // Example 2: String Contains Filter
    const char* sql2 = "SELECT COUNT(*) FROM 'merged_part' WHERE 2 CONTAINS 'Data'";
    printf("\nSQL: %s\n", sql2);
    LomoQueryPlan plan2 = lomo_parse_sql(sql2);
    lomo_execute_plan(&plan2);

    return 0;
}
