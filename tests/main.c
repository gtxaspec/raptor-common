#include "greatest.h"

extern SUITE(config_suite);
extern SUITE(util_suite);
extern SUITE(json_suite);
extern SUITE(http_suite);
extern SUITE(file_suite);

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	RUN_SUITE(config_suite);
	RUN_SUITE(util_suite);
	RUN_SUITE(json_suite);
	RUN_SUITE(http_suite);
	RUN_SUITE(file_suite);
	GREATEST_MAIN_END();
}
